# Script for loading Asterisk config files into the Asterisk Database
# To be used with Asterisk Database realtime configuration engine
# 
# Copyright (C) 2006 Sun Microsystems
#
# Written by Tony Yu Zhao <tony.yu.zhao@gmail.com>
#
#
# Usage
#    perl ast2astdb.pl <configuration filename>
#    
#    Make sure the host, port, user, and pass work matches the configuration in manager.conf
#    This utility uses the Asterisk manager interface
#
# Asterisk Database Storage Format 
#	  Family               Key            Value
#    ---------------------------------------------------------
#    filename             category.var   value1;value2;value3                            	  
#
# Works with multiple values of the same variable and #includes
# Order enforced for allow/disallow, Example: /sip.conf/general.allow !all,gsm,!ulaw,... 
#
# Limitations:
# 1. Only static database configuration available currently
# 2. Should work for all configuration files except extensions.conf (Useful for iax.conf, sip.conf, and voicemail.conf)
# 3. Order not enforced except for allow, disallow                         	  
#!/usr/bin/perl -w

use strict;
use POSIX;
use Socket;

# Declare the subroutines
sub trim($);

if (@ARGV != 5) {
    print STDERR "Usage: ast2astdb <ast_config_file> <host> <port> <user> <password>\n";
    exit 1;
}

my $host  = $ARGV[1];
my $port  = $ARGV[2]; 
my $user = $ARGV[3];
my $pass = $ARGV[4];

open(CONFIG_FILE, "<$ARGV[0]") || die $!;

my @lines;
my $cat_metric = -1; # incremented to 0 on first hit
my $var_metric = -1;
my $category;
my $last_cat;
my $last_var;
my $delim_var_val;
my $allow_var_val = "";
my $multi = "false";
while (<CONFIG_FILE>) {
    my $line = $_;
    chop($line);
    my($var_name, $var_val);

    next if ($line =~ /^\s*;/); # comment line skip

    if ($line =~ /^\s*\[(.*?)\]/) { #category/context
        $category = $1;
        $var_metric = -1;
        $cat_metric++;
    } elsif ($line =~ /^\s*(\#include)\s\s*(.+)\s*$/) {   #include (you must manually load in your included file)
        $var_metric++;
        $var_name = $1; 
        $var_val = $2; 
    } elsif ($line =~ /^\s*(\w+)\s*=>\s*(.+)\s*;.*$/ ||
             $line =~ /^\s*(\w+)\s*=\s*(.+)\s*;.*$/ ||
             $line =~ /^\s*(\w+)\s*=>\s*(.+)\s*;?.*$/ ||
             $line =~ /^\s*(\w+)\s*=\s*(.+)\s*;?.*$/ ) {   #everything else and remove inline comments
        $var_metric++;
        $var_name = $1;
        $var_val  = $2;
    } else {
        next; # no match, skip
    }

    if ($var_metric >= 0) {
        if( $last_cat eq trim($category) && $last_var eq trim($var_name)
            && trim($var_name) ne "allow" && trim($var_name) ne "disallow") {
          # same variable with multiple values except allow and disallow
          # accumulated in delim_var_val
          if( $delim_var_val ne "") {
             $delim_var_val = $delim_var_val . ";" . trim($var_val);         
          } else {
             $delim_var_val = trim($var_val);
          }
        } elsif( $last_cat eq trim($category)
            && (trim($var_name) eq "allow" || trim($var_name) eq "disallow")) {
          # allow and disallow from the same category
          # accumulated in allow_var_val
          if( trim($var_name) eq "allow") {
             if( $allow_var_val ne "") {
                $allow_var_val = $allow_var_val . ";" . trim($var_val);  
             } else {
                $allow_var_val = trim($var_val); 
             }
          } elsif( trim($var_name) eq "disallow") {
             if( $allow_var_val ne "") {
                $allow_var_val = $allow_var_val . ";!" . trim($var_val);  
             } else {
                $allow_var_val = "!" . trim($var_val); 
             } 
          }
        } else { 
           # different category or different var_name except allow,disallow
           # push the last object to the hash
           if( $allow_var_val ne "") {
              # allow_var_val must stored since this means means a different category is reached
              print "allow: category = $last_cat, var_val = $allow_var_val\n"; 
              my %hash = ('category'   => trim($last_cat),
                          'var_name'   => trim("allow"),
                          'var_val'    => trim($allow_var_val));
              push(@lines, \%hash);
              $allow_var_val = "";           
           } 
           if(trim($last_var) ne "allow" && trim($last_var) ne "disallow") {
              # delim_var_val is stored, either a different category or different var_name
              my %hash = ('category'   => trim($last_cat),
                          'var_name'   => trim($last_var),
                          'var_val'    => trim($delim_var_val));
              push(@lines, \%hash);
           }
              
           $last_cat = trim($category); 
           $last_var = trim($var_name);
           if(trim($var_name) eq "allow" || trim($var_name) eq "disallow") {
              # have to be careful when allow, disallow is the first entry of a new category
              if( trim($var_name) eq "allow") {
                 if( $allow_var_val ne "") {
                   $allow_var_val = $allow_var_val . ";" . trim($var_val);  
                 } else {
                    $allow_var_val = trim($var_val); 
                 }
              } elsif( trim($var_name) eq "disallow") {
                 if( $allow_var_val ne "") {
                    $allow_var_val = $allow_var_val . ";!" . trim($var_val);  
                 } else {
                    $allow_var_val = "!" . trim($var_val); 
                 } 
              } 
           } else {
              $delim_var_val = trim($var_val);
           }
        }   
    }
}

# push the final object to the hash
my %hash = ('category'   => trim($last_cat),
            'var_name'   => trim($last_var),
            'var_val'    => trim($delim_var_val));
push(@lines, \%hash);

if( $allow_var_val ne "") {
   my %hash = ('category'   => trim($last_cat),
               'var_name'   => trim("allow"),
               'var_val'    => trim($allow_var_val));
   push(@lines, \%hash);
}



close(CONFIG_FILE);

#connect to Asterisk Manager 
if ($port =~ /\D/) { $port = getservbyname($port, 'tcp') }
   die "No port" unless $port;

my $proto   = getprotobyname('tcp');
socket(SOCK, PF_INET, SOCK_STREAM, $proto)	|| die "socket: $!";
my $iaddr   = inet_aton("127.1") 		|| die "no host: $host";
my $name  = gethostbyaddr($iaddr, AF_INET);
my $paddr   = sockaddr_in($port, $iaddr);
connect(SOCK, $paddr)    || die "connect: $!";

#login
print SOCK "action: login\015\012";
print SOCK "username: $user\015\012";
print SOCK "secret: $pass\015\012";
print SOCK "\015\012";

my $categories = "";
my $data = "";
my $filename;

#remove extra / in the file path, we only want the actual file name
if ($ARGV[0] =~ /^\s*.*\/(.+)\s*$/) {
   $filename = $1;
} else {
   $filename = $ARGV[0];
}

#remove all previous stored database entires for filename
print "database deltree $filename \015\012";
print SOCK "action: command\015\012";
print SOCK "command: database deltree $filename \015\012";
print SOCK "\015\012";

#iterate over all data and store it into Asterisk Database
foreach my $row (@lines) {
    #print "$row->{'category'}\t$row->{'var_name'}\t$row->{'var_val'}\n";
     
    if ($row->{'category'} ne "" && $row->{'var_name'} ne "" && $row->{'var_val'} ne "" ) {
      #add the entry
      print "database put $filename $row->{'category'}.$row->{'var_name'} \"$row->{'var_val'}\"\015\012";
      print SOCK "action: command\015\012";
      print SOCK "command: database put $filename $row->{'category'}.$row->{'var_name'} \"$row->{'var_val'}\"\015\012";
      print SOCK "\015\012";
    }  
}
close (SOCK)	    || die "close: $!";
exit;

# Perl trim function to remove whitespace and comments from the start and end of the string
sub trim($)
{
	my $string = shift;
	$string =~ s/^\s+//;
	$string =~ s/\s+$//;
	return $string;
}



