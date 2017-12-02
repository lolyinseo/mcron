/*                                                   -*-c-*- */
/*
 *   Copyright (C) 2003, 2014 Dale Mellor
 * 
 *   This file is part of GNU mcron.
 *
 *   GNU mcron is free software: you can redistribute it and/or modify it under
 *   the terms of the GNU General Public License as published by the Free
 *   Software Foundation, either version 3 of the License, or (at your option)
 *   any later version.
 *
 *   GNU mcron is distributed in the hope that it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *   more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with GNU mcron.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
    This C code represents the thinnest possible wrapper around the Guile code
    which constitutes all the functionality of the mcron program. There are two
    plus one reasons why we need to do this, and one very unfortunate
    consequence.

        Firstly, SUID does not work on an executable script. In the end, it is
        the execution of the translator, in our case guile, which determines the
        effective user, and it is not wise to make the system guile installation
        SUID root!

        Secondly, executable scripts show up in ugly ways in listings of the
        system process table. Guile in particular, with its multi-line
        #! ...\ \n -s ...!#
        idiosyncracies shows up in process listings in a way that is difficult
        to determine what program is actually running.

        A third reason for the C wrapper which might be mentioned is that a
        security-conscious system administrator can choose to only install a
        binary, thus removing the possibility of a user studying a guile script
        and working out ways of hacking it to his own ends, or worse still
        finding a way to modify it to his own ends.

        Unfortunately, running the guile script from inside a C program means
        that the sigaction function does not work. Instead, it is necessary to
        perform the signal processing in C.

    The guile code itself is substituted for the GU1LE_PROGRAM_GOES_HERE (sic)
    token by the makefile, which processes the scheme to make it look like one
    big string.
*/



#include <string.h>
#include <signal.h>
#include <libguile.h>



/* This is a function designed to be installed as a signal handler, for signals
   which are supposed to initiate shutdown of this program. It calls the scheme
   procedure (see mcron.scm for details) to do all the work, and then exits. */

void
react_to_terminal_signal (int sig)
{
  scm_c_eval_string ("(delete-run-file)");
  exit (1);
}



/* This is a function designed to be callable from scheme, and sets up all the
   signal handlers required by the cron personality.  */

SCM
set_cron_signals ()
{
  static struct sigaction sa;
  memset (&sa, 0, sizeof (sa));
  sa.sa_handler = react_to_terminal_signal;
  sigaction (SIGTERM, &sa, 0);
  sigaction (SIGINT,  &sa, 0);
  sigaction (SIGQUIT, &sa, 0);
  sigaction (SIGHUP,  &sa, 0);
  
  return SCM_BOOL_T;
}



/* The effective main function (i.e. the one that actually does some work). We
   register the function above with the guile system, and then execute the mcron
   guile program. */

void
inner_main ()
{
  scm_c_define_gsubr ("c-set-cron-signals", 0, 0, 0, set_cron_signals);
    
  scm_c_eval_string (
"(use-modules (mcron config))"
"(if config-debug (begin (debug-enable 'debug)"
"                        (debug-enable 'backtrace)))"
"(use-modules (ice-9 regex) (ice-9 rdelim))"
"(define command-name (match:substring (regexp-exec (make-regexp \"[[:alpha:]]*$\")"
"                                                   (car (command-line)))))"
"(define (mcron-error exit-code . rest)"
"  (with-output-to-port (current-error-port)"
"    (lambda ()"
"      (for-each display (append (list command-name \": \") rest))"
"      (newline)))"
"  (if (and exit-code (not (eq? exit-code 0)))"
"      (primitive-exit exit-code)))"
"(defmacro catch-mcron-error (. body)"
"  `(catch 'mcron-error"
"          (lambda ()"
"            ,@body)"
"          (lambda (key exit-code . msg)"
"            (apply mcron-error exit-code msg))))"
"(define command-type (cond ((string=? command-name \"mcron\") 'mcron)"
"                           ((or (string=? command-name \"cron\")"
"                                (string=? command-name \"crond\")) 'cron)"
"                           ((string=? command-name \"crontab\") 'crontab)"
"                           (else"
"                            (mcron-error 12 \"The command name is invalid.\"))))"
"(use-modules (ice-9 getopt-long))"
"(define options"
"  (catch"
"   'misc-error"
"   (lambda ()"
"     (getopt-long (command-line)"
"                  (append"
"                   (case command-type"
"                     ((crontab)"
"                      '((user     (single-char #\\u) (value #t))"
"                        (edit     (single-char #\\e) (value #f))"
"                        (list     (single-char #\\l) (value #f))"
"                        (remove   (single-char #\\r) (value #f))))"
"                     (else `((schedule (single-char #\\s) (value #t)"
"                                       (predicate"
"                                        ,(lambda (value)"
"                                           (string->number value))))"
"                             (daemon   (single-char #\\d) (value #f))"
"                             (noetc    (single-char #\\n) (value #f))"
"                             (stdin    (single-char #\\i) (value #t)"
"                                       (predicate"
"                                        ,(lambda (value)"
"                                           (or (string=? \"vixie\" value)"
"                                               (string=? \"guile\" value))))))))"
"                   '((version  (single-char #\\v) (value #f))"
"                     (help     (single-char #\\h) (value #f))))))"
"   (lambda (key func fmt args . rest)"
"     (mcron-error 1 (apply format (append (list #f fmt) args))))))"
"(if (option-ref options 'version #f)"
"    (begin"
"      (display (string-append \"\\n"
"\" command-name \"  (\" config-package-string \")\\n"
"Written by Dale Mellor\\n"
"\\n"
"Copyright (C) 2003, 2006, 2014  Dale Mellor\\n"
"This is free software; see the source for copying conditions.  There is NO\\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\\n"
"\"))"
"      (quit)))"
"(if (option-ref options 'help #f)"
"    (begin"
"      (display (string-append \""
"Usage: \" (car (command-line))"
"(case command-type"
"  ((mcron)"
"\" [OPTIONS] [FILES]\\n"
"Run an mcron process according to the specifications in the FILES (`-' for\\n"
"standard input), or use all the files in ~/.config/cron (or the \\n"
"deprecated ~/.cron) with .guile or .vixie extensions.\\n"
"\\n"
"  -v, --version             Display version\\n"
"  -h, --help                Display this help message\\n"
"  -sN, --schedule[=]N       Display the next N jobs that will be run by mcron\\n"
"  -d, --daemon              Immediately detach the program from the terminal\\n"
"                              and run as a daemon process\\n"
"  -i, --stdin=(guile|vixie) Format of data passed as standard input or\\n"
"                              file arguments (default guile)\")"
"  ((cron)"
"\" [OPTIONS]\\n"
"Unless an option is specified, run a cron daemon as a detached process, \\n"
"reading all the information in the users' crontabs and in /etc/crontab.\\n"
"\\n"
"  -v, --version             Display version\\n"
"  -h, --help                Display this help message\\n"
"  -sN, --schedule[=]N       Display the next N jobs that will be run by cron\\n"
"  -n, --noetc               Do not check /etc/crontab for updates (HIGHLY\\n"
"                              RECOMMENDED).\")"
"  ((crontab)"
"           (string-append \" [-u user] file\\n\""
"           \"       \" (car (command-line)) \" [-u user] { -e | -l | -r }\\n\""
"           \"               (default operation is replace, per 1003.2)\\n\""
"           \"       -e      (edit user's crontab)\\n\""
"           \"       -l      (list user's crontab)\\n\""
"           \"       -r      (delete user's crontab)\\n\"))"
"  (else \"rubbish\"))"
"\"\\n\\n"
"Report bugs to \" config-package-bugreport \".\\n"
"\"))"
"      (quit)))"
"(define (delete-run-file)"
"  (catch #t (lambda () (delete-file config-pid-file)"
"                       (delete-file config-socket-file))"
"            noop)"
"  (quit))"
"(if (eq? command-type 'cron)"
"    (begin"
"      (if (not (eqv? (getuid) 0))"
"          (mcron-error 16"
"                       \"This program must be run by the root user (and should \""
"                       \"have been installed as such).\"))"
"      (if (access? config-pid-file F_OK)"
"          (mcron-error 1"
"		       \"A cron daemon is already running.\\n\""
"		       \"  (If you are sure this is not true, remove the file\\n\""
"		       \"   \""
"		       config-pid-file"
"		       \".)\"))"
"      (if (not (option-ref options 'schedule #f))"
"          (with-output-to-file config-pid-file noop))"
"      (setenv \"MAILTO\" #f)"
"      (c-set-cron-signals)))"
"(use-modules (mcron core)"
"             (mcron job-specifier)"
"             (mcron vixie-specification))"
"(define (stdin->string)"
"  (with-output-to-string (lambda () (do ((in (read-char) (read-char)))"
"                                        ((eof-object? in))"
"                                        (display in)))))"
"(if (eq? command-type 'crontab)"
"    (begin"
"(let ((hit-server"
"       (lambda (user-name)"
"         (catch #t (lambda ()"
"                     (let ((socket (socket AF_UNIX SOCK_STREAM 0)))"
"                       (connect socket AF_UNIX config-socket-file)"
"                       (display user-name socket)"
"                       (close socket)))"
"                (lambda (key . args)"
"                  (display \"Warning: a cron daemon is not running.\\n\")))))"
"      (in-access-file?"
"       (lambda (file name)"
"         (catch #t (lambda ()"
"                     (with-input-from-file"
"                         file"
"                       (lambda ()"
"                         (let loop ((input (read-line)))"
"                           (if (eof-object? input)"
"                               #f"
"                               (if (string=? input name)"
"                                   #t"
"                                   (loop (read-line))))))))"
"                (lambda (key . args) '()))))"
"      (crontab-real-user (passwd:name (getpw (getuid)))))"
"  (if (or (eq? (in-access-file? config-allow-file crontab-real-user) #f)"
"          (eq? (in-access-file? config-deny-file crontab-real-user) #t))"
"      (mcron-error 6 \"Access denied by system operator.\"))"
"  (if (> (+ (if (option-ref options 'edit #f) 1 0)"
"            (if (option-ref options 'list #f) 1 0)"
"            (if (option-ref options 'remove #f) 1 0))"
"         1)"
"      (mcron-error 7 \"Only one of options -e, -l or -r can be used.\"))"
"  (if (and (not (eqv? (getuid) 0))"
"           (option-ref options 'user #f))"
"      (mcron-error 8 \"Only root can use the -u option.\"))"
"  (let ("
"        (crontab-user (option-ref options 'user crontab-real-user))"
"        (crontab-file (string-append config-spool-dir \"/\" crontab-user))"
"        (get-yes-no (lambda (prompt . re-prompt)"
"                      (if (not (null? re-prompt))"
"                          (display \"Please answer y or n.\\n\"))"
"                      (display (string-append prompt \" \"))"
"                      (let ((r (read-line)))"
"                        (if (not (string-null? r))"
"                            (case (string-ref r 0)"
"                              ((#\\y #\\Y) #t)"
"                              ((#\\n #\\N) #f)"
"                              (else (get-yes-no prompt #t)))"
"                            (get-yes-no prompt #t))))))"
"    (cond"
" ((option-ref options 'list #f)"
"  (catch #t (lambda ()"
"              (with-input-from-file crontab-file (lambda ()"
"                 (do ((input (read-char) (read-char)))"
"                     ((eof-object? input))"
"                   (display input)))))"
"         (lambda (key . args)"
"           (display (string-append \"No crontab for \""
"                                   crontab-user"
"                                   \" exists.\\n\")))))"
" ((option-ref options 'edit #f)"
"  (let ((temp-file (string-append config-tmp-dir"
"                                  \"/crontab.\""
"                                  (number->string (getpid)))))"
"    (catch #t (lambda () (copy-file crontab-file temp-file))"
"              (lambda (key . args) (with-output-to-file temp-file noop)))"
"    (chown temp-file (getuid) (getgid))"
"    (let retry ()"
"      (system (string-append"
"               (or (getenv \"VISUAL\") (getenv \"EDITOR\") \"vi\")"
"               \" \""
"               temp-file))"
"      (catch 'mcron-error"
"             (lambda () (read-vixie-file temp-file))"
"             (lambda (key exit-code . msg)"
"               (apply mcron-error 0 msg)"
"               (if (get-yes-no \"Edit again?\")"
"                   (retry)"
"                   (begin"
"                     (mcron-error 0 \"Crontab not changed\")"
"                     (primitive-exit 0))))))"
"    (copy-file temp-file crontab-file)"
"    (delete-file temp-file)"
"    (hit-server crontab-user)))"
" ((option-ref options 'remove #f)"
"  (catch #t (lambda () (delete-file crontab-file)"
"                       (hit-server crontab-user))"
"            noop))"
" ((not (null? (option-ref options '() '())))"
"  (let ((input-file (car (option-ref options '() '()))))"
"    (catch-mcron-error"
"     (if (string=? input-file \"-\")"
"         (let ((input-string (stdin->string)))"
"           (read-vixie-port (open-input-string input-string))"
"           (with-output-to-file crontab-file (lambda ()"
"                                               (display input-string))))"
"         (begin"
"           (read-vixie-file input-file)"
"           (copy-file input-file crontab-file))))"
"    (hit-server crontab-user)))"
" (else"
"  (mcron-error 15 \"usage error: file name must be specified for replace.\")))"
")) "
"      (quit)))"
"(define (regular-file? file)"
"  (catch 'system-error"
"	 (lambda ()"
"	   (eq? (stat:type (stat file)) 'regular))"
"	 (lambda (key call fmt args . rest)"
"	   (mcron-error 0 (apply format (append (list #f fmt) args)))"
"	   #f)))"
"(define guile-file-regexp (make-regexp \"\\\\.gui(le)?$\"))"
"(define vixie-file-regexp (make-regexp \"\\\\.vix(ie)?$\"))"
"(define (process-user-file file-path . assume-guile)"
"  (cond ((string=? file-path \"-\")"
"               (if (string=? (option-ref options 'stdin \"guile\") \"vixie\")"
"                   (read-vixie-port (current-input-port))"
"                   (eval-string (stdin->string))))"
"        ((or (not (null? assume-guile))"
"             (regexp-exec guile-file-regexp file-path))"
"         (load file-path))"
"        ((regexp-exec vixie-file-regexp file-path)"
"         (read-vixie-file file-path))))"
"(define (process-files-in-user-directory)"
"  (let ((errors 0)"
"        (home-directory (passwd:dir (getpw (getuid)))))"
"    (map (lambda (config-directory)"
"          (catch #t"
"                 (lambda ()"
"                   (let ((directory (opendir config-directory)))"
"                     (do ((file-name (readdir directory) (readdir directory)))"
"                         ((eof-object? file-name) (closedir directory))"
"                       (process-user-file (string-append config-directory"
"                                                         \"/\""
"                                                         file-name)))))"
"                 (lambda (key . args)"
"                   (set! errors (1+ errors)))))"
"          (list (string-append home-directory \"/.cron\")"
"                (string-append (or (getenv \"XDG_CONFIG_HOME\")"
"                                   (string-append home-directory \"/.config\"))"
"                               \"/cron\")))"
"    (if (eq? 2 errors)"
"        (mcron-error 13"
"                     \"Cannot read files in your ~/.config/cron (or ~/.cron) \""
"                     \"directory.\"))))"
"(define (valid-user user-name)"
"  (setpwent)"
"  (do ((entry (getpw) (getpw)))"
"      ((or (not entry)"
"           (string=? (passwd:name entry) user-name))"
"       (endpwent)"
"       entry)))"
"(use-modules (srfi srfi-2)) "
"(define (process-files-in-system-directory)"
"  (catch #t"
"         (lambda ()"
"           (let ((directory (opendir config-spool-dir)))"
"             (do ((file-name (readdir directory) (readdir directory)))"
"                 ((eof-object? file-name))"
"               (and-let* ((user (valid-user file-name)))"
"                         (set-configuration-user user) "
"                         (catch-mcron-error"
"                          (read-vixie-file (string-append config-spool-dir"
"                                                          \"/\""
"                                                          file-name)))))))"
"         (lambda (key . args)"
"           (mcron-error"
"            4"
"            \"You do not have permission to access the system crontabs.\"))))"
"(case command-type"
"  ((mcron) (if (null? (option-ref options '() '()))"
"                (process-files-in-user-directory)"
"                (for-each (lambda (file-path)"
"                            (process-user-file file-path #t))"
"                          (option-ref options '() '()))))"
"  ((cron) (process-files-in-system-directory)"
"   (use-system-job-list)"
"   (catch-mcron-error"
"    (read-vixie-file \"/etc/crontab\" parse-system-vixie-line))"
"   (use-user-job-list)"
"   (if (not (option-ref options 'noetc #f))"
"       (begin"
"         (display"
"\"WARNING: cron will check for updates to /etc/crontab EVERY MINUTE. If you do\\n"
"not use this file, or you are prepared to manually restart cron whenever you\\n"
"make a change, then it is HIGHLY RECOMMENDED that you use the --noetc\\n"
"option.\\n\")"
"         (set-configuration-user \"root\")"
"         (job '(- (next-minute-from (next-minute)) 6)"
"              check-system-crontab"
"              \"/etc/crontab update checker.\")))))"
"(and-let* ((count (option-ref options 'schedule #f)))"
"          (set! count (string->number count))"
"          (display (get-schedule (if (<= count 0) 1 count)))"
"          (quit))"
"(if (option-ref options 'daemon (eq? command-type 'cron))"
"    (begin"
"      (if (not (eqv? (primitive-fork) 0))"
"          (quit))"
"      (setsid)"
"      (if (eq? command-type 'cron)"
"          (with-output-to-file config-pid-file"
"            (lambda () (display (getpid)) (newline))))))"
"(define fd-list '())"
"(if (eq? command-type 'cron)"
"    (catch #t"
"           (lambda ()"
"             (let ((socket (socket AF_UNIX SOCK_STREAM 0)))"
"               (bind socket AF_UNIX config-socket-file)"
"               (listen socket 5)"
"               (set! fd-list (list socket))))"
"           (lambda (key . args)"
"             (delete-file config-pid-file)"
"             (mcron-error 1"
"                          \"Cannot bind to UNIX socket \""
"                          config-socket-file))))"
"		     "
"(define (process-update-request)"
"  (let* ((socket (car (accept (car fd-list))))"
"         (user-name (read-line socket)))"
"    (close socket)"
"    (set-configuration-time (current-time))"
"    (catch-mcron-error"
"     (if (string=? user-name \"/etc/crontab\")"
"         (begin"
"           (clear-system-jobs)"
"           (use-system-job-list)"
"           (read-vixie-file \"/etc/crontab\" parse-system-vixie-line)"
"           (use-user-job-list))"
"         (let ((user (getpw user-name)))"
"           (remove-user-jobs user)"
"           (set-configuration-user user)"
"           (read-vixie-file (string-append config-spool-dir \"/\" user-name)))))))"
"(catch-mcron-error"
" (while #t"
"        (run-job-loop fd-list)"
"        (if (not (null? fd-list))"
"            (process-update-request))))"
                     );
}



/* The real main function. Does nothing but start up the guile subsystem. */

int
main (int argc, char **argv)
{
  setenv ("GUILE_LOAD_PATH", GUILE_LOAD_PATH, 1);
  
  scm_boot_guile (argc, argv, inner_main, 0);
  
  return 0;
}
