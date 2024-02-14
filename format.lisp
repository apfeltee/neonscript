(
(setq rcs-format-
   "$Header")
(declare (special Format-Standard-Output roman-old 
		  format-params-supplied format format-handlers
		  format-sharpsign-vars))
(setq format-sharpsign-vars @franz-symbolic-character-names)
(or (boundp @roman-old) (setq roman-old nil))
(declare (setq defmacro-for-compiling nil defmacro-displace-call nil ))
  (defmacro nsubstring (&rest w) `(format\:nsubstring ,.w))
  (defmacro string-search-char (&rest w) `(format\:string-search-char ,.w))
  (defmacro ar-1 (ar ind) `(cxr ,ind ,ar))
  (defmacro as-1 (val ar ind) `(rplacx ,ind ,ar ,val))
  (defmacro >= (x y) `(not (< ,x ,y)))
  (defmacro <= (x y) `(not (> ,x ,y)))
  (defmacro neq (x y) `(not (= ,x ,y)))
  (defmacro pop (stack) `(prog1 (car ,stack) (setq ,stack (cdr ,stack))))
(declare (setq defmacro-for-compiling @t defmacro-displace-call @t))
(declare
 (special ctl-string		 
	  ctl-length		 
	  ctl-index		 
	  atsign-flag		 
	  colon-flag		 
	  format-temporary-area	 
	  format-arglist	 
	  arglist-index		 
	  float-format		 
	  poport		
	  ))
(defun format (stream ctl-string &rest args)
  (let (format-string Format-Standard-Output
	(format-arglist args)
	(arglist-index 0))
    (setq stream (cond ((eq stream @t) poport )
		       ((null stream)
			(setq format-string @t)
			(list nil))
		       (t stream)))
    (setq Format-Standard-Output stream)
    (cond ((symbolp ctl-string)
	   (setq ctl-string (get_pname ctl-string))))
    (cond ((stringp ctl-string)
	   (format-ctl-string args ctl-string))
	  (t (do ((ctl-string ctl-string (cdr ctl-string)))
	       ((null ctl-string))
	       (setq args
		     (cond ((symbolp (car ctl-string))
			    (format-ctl-string args (car ctl-string)))
			   (t (format-ctl-list args (car ctl-string))))))))
    (and format-string
	 (setq format-string (maknam (nreverse (cdr stream)))))
    format-string))
(defun format-ctl-list (args ctl-list)
       (format-ctl-op (car ctl-list) args (cdr ctl-list)))
(defun format-ctl-string (args ctl-string)
    (declare (fixnum ctl-index ctl-length))
    (do   ((ctl-index 0) (ch) (tem) (str) (sym)
	   (ctl-length (flatc ctl-string)))
	  ((>= ctl-index ctl-length) args)
	(setq tem (cond ((string-search-char #/~ ctl-string ctl-index))
			(t ctl-length)))
	(cond ((neq tem ctl-index)		
	       (setq str (nsubstring ctl-string ctl-index tem))
	       (format:patom str)
	       (and (>= (setq ctl-index tem) ctl-length)
		    (return args))))
	(do ((atsign-flag nil)	
	     (colon-flag nil)	
	     (params (makhunk 10))
	     (param-leader -1)
	     (param-flag nil)	
	     (param))		
	    ((>= (setq ctl-index (1+ ctl-index)) ctl-length))
	  (setq ch (getcharn ctl-string (1+ ctl-index)))
	  (cond ((and (>= ch #/0) (<= ch #/9))		 	 
		 (setq param (+ (* (or param 0) 10.) (- ch #/0)) 
		     param-flag t))
		((= ch #/@)					 
		 (setq atsign-flag t))
		((= ch #/:)					 
		 (setq colon-flag t))
		((or (= ch #/v) (= ch #/V))			
		 (as-1 (pop args) params
		       (setq param-leader (1+ param-leader)))
		 (setq arglist-index (1+ arglist-index))
		 (setq param nil param-flag nil))
		((= ch #/#)
		 (as-1 (length args) params
		       (setq param-leader (1+ param-leader))))
		((= ch #/,)
		 (and param-flag (as-1 param params (setq param-leader
							  (1+ param-leader))))
		 (setq param nil param-flag t))
		(t		
		  (and (>= ch #/A) (<= ch #/Z) (setq ch (+ ch (- #/a #/A))))
		  (setq ctl-index (1+ ctl-index)) 
		  (and param-flag (as-1 param params (setq param-leader
							   (1+ param-leader))))
		  (setq param-flag nil param nil tem nil)
		  (setq
		    str (cond ((= ch #/\ )			 
			       (let ((i (string-search-char
					   #/\
					   ctl-string
					   (1+ ctl-index))))
				  (and (null i)
				       (ferror nil
					  "Unmatched \\ in control string."))
				  (prog1 
				     (setq tem
					   (nsubstring ctl-string
						       (1+ ctl-index)
						       i))
				     (setq ctl-index i))))
			      ((= ch #\newline) (concat "ch" ch))  
			      (t (ascii ch))))
		  (cond ((setq sym str)
			 (setq format-params-supplied param-leader)
			 (setq args (format-ctl-op sym args params)))
			(t (ferror nil "~C is an unknown FORMAT op in \"~A\""
				   tem ctl-string)))
		  (return nil))))))
(defun format-ctl-op (op args params &aux tem)
   (cond ((stringp op) (setq op (concat op))))  
   (cond ((setq tem (assq op format-handlers))
	  (cond ((eq @one (cadr tem))
		 (or args
		     (ferror nil "arg required for ~a, but no more args" op))
		 (funcall (caddr tem) (car args) params)
		 (setq arglist-index (1+ arglist-index))
		 (cdr args))
		((eq @none (cadr tem))
		 (funcall (caddr tem) params)
		 args)
		((eq @many (cadr tem))
		 (funcall (caddr tem) args params))
		(t (ferror nil "Illegal format handler: ~s" tem))))
	 (t (ferror nil "\"~S\" is not defined as a FORMAT command." op)
	    args)))
(setq format-handlers nil)
(defmacro defformat (name arglist type &rest body)
   (let (newname)
      (cond ((fixp name) (setq name (concat "ch" name))))
      (cond ((not (memq type (none one many)))
	     (ferror nil "The format type, \"~a\" is not: none, one or many"
		     type)))
      (cond ((or (not (symbolp name))
		 (not (dtpr arglist)))
	     (ferror nil "Bad form for name and/or arglist: ~a ~a"
		     name arglist)))
      (cond ((memq type (one many))
	     (cond ((not (= (length arglist) 2))
		    (ferror nil "There should be 2 arguments to ~a" name))))
	    (t (cond ((not (= (length arglist) 1))
		      (ferror nil "There should be 1 argument to ~a" name)))))
      (setq newname (concat name ":format-handler"))
      `(progn @compile
	      (defun ,newname ,arglist ,@body)
	      (let ((handler (assq @,name format-handlers)))
		 (cond (handler (rplaca (cddr handler) @,newname))
		       (t (setq format-handlers (cons (list @,name
							    @,type
							    @,newname)
						      format-handlers))))))))
(defformat d (arg params) one
   (let ((width (cxr 0 params))
	 (padchar (cxr 1 params)))
    (cond ((and colon-flag (< arg 4000.) (> arg 0))
	   (roman-step arg 0))
	  (atsign-flag (english-print arg @cardinal))
	  ((let ((base 10.) (*nopoint t))
	     (cond ((null padchar) (setq padchar 32.))
		   ((not (numberp padchar))
		    (setq padchar (getcharn padchar 1))))
	     (and width (format-ctl-justify width (flatc arg) padchar))
	     (format:patom arg))))))
(defformat f (arg params) one
   (cond ((not (floatp arg)) (format:patom arg))
	 (t (let ((float-format "%.16g")
		  (prec (cxr 0 params)))
	       (cond ((and prec (fixp prec) (> prec 0) (< prec 16))
		      (setq float-format (concat "%" prec "g"))))
	       (format:patom arg)))))
(defformat r (arg params) one
   (format:anyradix-printer arg params nil))
(defformat o (arg params) one
   (format:anyradix-printer arg params 8.))
(defun format:anyradix-printer (arg params radix)
   (let ((paramstart (cond (radix 0)
			   (t 1))))
      (cond ((null radix) (setq radix (cxr 0 params))))
      (cond ((null radix)	
	     (cond ((and (null colon-flag) (null atsign-flag))
		    (english-print arg @cardinal))
		   ((and colon-flag (null atsign-flag))
		    (english-print arg @ordinal))
		   ((and (null colon-flag) atsign-flag)
		    (roman-step arg 0))
		   ((and colon-flag atsign-flag)
		    (let ((roman-old t))
		       (roman-step arg 0)))))
	    (t (let ((mincol (cxr paramstart params))
		     (padchr (or (cxr (+ 1 paramstart) params) #\space))
		     (res))
		  (cond (mincol 	
			   (let ((Format-Standard-Output (list nil)))
			      (format-binpr arg radix)
			      (setq res (cdr Format-Standard-Output)))
			   (format-ctl-justify mincol (length res) padchr)
			   (mapc @format:tyo (nreverse res)))
			(t (format-binpr arg radix))))))))
(defun format-binpr (x base)
   (cond ((equal x 0)(format:patom 0))
	 ((or (> base 36.) (< base 2))
	  (ferror nil "\"~s\" is not a base between 2 and 36" base))
	 ((lessp x 0)
	  (format:patom @-)
	  (format-binpr1 (minus x) base))
	 (t (format-binpr1 x base)))
   x)
(defun format-binpr1 (x base)
   (cond ((equal x 0))
	 (t (format-binpr1 (quotient x base) base)
	    (format-prc (remainder x base)))))
(defun format-prc (x)
   (cond ((< x 10.) (format:patom x))
	 (t (format:tyo (plus (- #/a 10.) x)))))
(defun format-ctl-octal (arg params)
   (let ((width (cxr 0 params)) (padchar (cxr 1 params)))
      (let ((base 8))
	 (cond ((null padchar)
		(setq padchar 32.))
	       ((not (numberp padchar))
		(setq padchar (getcharn padchar 1))))
	 (and width (format-ctl-justify width (flatc arg) padchar))
	 (format:patom arg))))
(defformat a (arg params) one
   (format-ctl-ascii arg params nil))
(defun format-ctl-ascii (arg params prin1p)
    (let ((edge (cxr 0 params))
	  (period (cxr 1 params))
          (min (cxr 2 params))
	  (padchar (cxr 3 params)))
	 (cond ((null padchar)
		(setq padchar #\space))
	       ((not (numberp padchar))
		(setq padchar (getcharn padchar 1))))
         (cond (prin1p (format:print arg))
               (t (format:patom arg)))
	 (cond ((not (null edge))
		(let ((width (cond (prin1p (flatsize arg)) ((flatc arg)))))
		  (cond ((not (null min))
			 (format-ctl-repeat-char min padchar)
			 (setq width (+ width min))))
		  (cond (period
			 (format-ctl-repeat-char
			  (- (+ edge (* (\\ (+ (- (max edge width) edge 1)
					       period)
					    period)
					period))
			     width)
			  padchar))
			(t (format-ctl-justify edge width padchar))))))))
(defformat s (arg params) one
    (format-ctl-ascii arg params t))
(defformat c (arg params) one
   (cond ((or (not (fixp arg))
	      (< arg 0)
	      (> arg 127))
	  (ferror nil "~s is not a legal character value" arg)))
   (cond ((and (not colon-flag) (not atsign-flag))
	  (format:patom (ascii arg)))
	 (t 
	    (let (name)
	       (cond ((setq name (car
				    (rassq arg (symeval format-sharpsign-vars))))
		      (cond (colon-flag (format:patom name))
			    (atsign-flag (format:patom "#\\")
					 (format:patom name))))
		     ((< arg #\space)
		      (setq arg (+ arg #/@))
		      (cond (colon-flag (format:patom "^")
					(format:patom (ascii arg)))
			    (atsign-flag (format:patom "#^")
					 (format:patom (ascii arg)))))
		     (t (cond (colon-flag (format:patom (ascii arg)))
			      (atsign-flag (format:patom "#/")
					   (format:patom (ascii arg))))))))))
(defformat p (args params) many
  (let (arg)
    (cond (colon-flag
	   (setq arg (nth (1- arglist-index) format-arglist)))
	  ((null args)
	   (ferror () "Argument required for p, but no more arguments"))
	  (t (setq arg (pop args)
		   arglist-index (1+ arglist-index))))
    (if (= arg 1)
	(if atsign-flag (format:tyo #/y))
	(cond (atsign-flag
	       (format:tyo #/i)
	       (format:tyo #/e)
	       (format:tyo #/s))
	      (t (format:tyo #/s))))
    args))
(defformat *  (args params) many
  (let ((count (or (cxr 0 params) 1)))
    (if colon-flag (setq count (minus count)))
    (setq arglist-index (+ arglist-index count))
    (nthcdr arglist-index format-arglist)))	
(defformat g (arg params) many
       (let ((count (or (cxr 0 params) 1)))
	    (nthcdr count format-arglist)))
(defformat % (params) none
       (declare (fixnum i))
       (let ((count (or (cxr 0 params) 1)))
	    (do i 0 (1+ i) (= i count)
		(format:terpr))))
(defformat #\newline (params) none
   (cond (atsign-flag
	    (format:tyo #\newline)))
   (cond ((not colon-flag)
	  (setq ctl-index (1+ ctl-index))
	  (do ()
	      ((>= ctl-index ctl-length))
	      (cond ((memq (getcharn ctl-string ctl-index)
			   (#\space #\tab))
		     (setq ctl-index (1+ ctl-index)))
		    (t (setq ctl-index (1- ctl-index))
		       (return)))))))
(defformat & (params) none
    (format:fresh-line))
(defformat x (params) none
   (format-ctl-repeat-char (cxr 0 params) #\space))

(defformat ~ (params) none
   (format-ctl-repeat-char (cxr 0 params) #/~))
(defun format-ctl-repeat-char (count char)
    (declare (fixnum i))
    (cond ((null count) (setq count 1)))
    (do i 0 (1+ i) (=& i count)
	(format:tyo char)))
(defun format-ctl-justify (width size &optional (char #\space))
    (and width (> width size) (format-ctl-repeat-char (- width size) char)))
(defformat q (arg params) one
   (do ((ii format-params-supplied (1- ii))
	(params-given nil))
       ((< ii 0) (apply arg params-given))
       (setq params-given (cons (cxr ii params) params-given))))



(declare (special english-small english-medium english-large))
(defun make-list-array (list)
   (let ((a (makhunk (length list))))
      (do ((i 0 (1+ i))
	   (ll list (cdr ll)))
	  ((null ll))
	  (rplacx i a (car ll)))
      a))
(setq english-small
   (make-list-array ("one" "two" "three" "four" "five" "six"
			    "seven" "eight" "nine" "ten" "eleven" "twelve"
			    "thirteen" "fourteen" "fifteen" "sixteen"
			    "seventeen" "eighteen" "nineteen")))
(setq english-medium
   (make-list-array ("twenty" "thirty" "forty" "fifty" "sixty" "seventy"
			       "eighty" "ninty")))
(setq english-large
   (make-list-array ("thousand" "million" "billion" "trillion" "quadrillion"
				 "quintillion")))
(defun english-print (n type)
       (declare (fixnum i n limit))
       (cond ((zerop n)
	      (cond ((eq type @cardinal) (format:patom "zero"))
		    (t (format:patom "zeroth"))))
	     ((< n 0)
	      (format:patom "minus")
	      (format:tyo #\space)
	      (english-print (minus n) type))
	     (t
	      (do ((n n)
                   (p)
		   (flag)
		   (limit 1000000.
			  (quotient limit 1000.))
		   (i 1 (1- i)))
		  ((< i 0)
		   (cond ((> n 0)
			  (and flag (format:tyo #\space))
			  (english-print-thousand n))))
		  (cond ((not (< n limit))
			 (setq p (quotient n limit)
			       n (remainder n limit))
			 (cond (flag (format:tyo #\space))
			       (t (setq flag t)))
			 (english-print-thousand p)
			 (format:tyo #\space)
			 (format:patom (ar-1 english-large i))))))))
(defun english-print-thousand (n)
       (declare (fixnum i n limit))
       (let ((n (remainder n 100.))
	     (h (quotient n 100.)))
	    (cond ((> h 0)
		   (format:patom (ar-1 english-small (1- h)))
		   (format:tyo #\space)
		   (format:patom "hundred")
		   (and (> n 0) (format:tyo #\space))))
	    (cond ((= n 0))
		  ((< n 20.)
		   (format:patom (ar-1 english-small (1- n))))
		  (t
		   (format:patom (ar-1 english-medium
						   (- (quotient n 10.) 2)))
		   (cond ((zerop (setq h (remainder n 10.))))
			 (t
			  (format:tyo #/-) 
			  (format:patom (ar-1 english-small (1- h)))))))))
(defun roman-step (x n)
    (cond ((> x 9.)
	   (roman-step (quotient x 10.) (1+ n))
	   (setq x (remainder  x 10.))))
    (cond ((and (= x 9) (not roman-old))
	   (roman-char 0 n)
	   (roman-char 0 (1+ n)))
	  ((= x 5)
	   (roman-char 1 n))
	  ((and (= x 4) (not roman-old))
	   (roman-char 0 n)
	   (roman-char 1 n))
	  (t (cond ((> x 5)
		    (roman-char 1 n)
		    (setq x (- x 5))))
	     (do i 0 (1+ i) (>= i x)
	       (roman-char 0 n)))))
(defun roman-char (i x)
    (format:tyo (car (nthcdr (+ i x x) (#/I #/V #/X #/L #/C #/D #/M)))
))
(defun format:tyo (char)
   (cond ((dtpr Format-Standard-Output)
	  (rplacd Format-Standard-Output
		  (cons char (cdr Format-Standard-Output))))
	 (t (tyo char Format-Standard-Output))))
(defun format:patom (arg)
   (format:printorpatom arg nil))
(defun format:print (arg)
   (format:printorpatom arg t))
(defun format:printorpatom (argument slashify)
   (cond ((dtpr Format-Standard-Output)
	  (rplacd Format-Standard-Output
		  (nreconc (cond (slashify
					 (mapcar (lambda (x)
						     (getcharn x 1))
					         (explode argument)))
					((exploden argument)))
				  (cdr Format-Standard-Output))))
	 (t (cond (slashify (print argument Format-Standard-Output))
		  (t (patom argument Format-Standard-Output))))))
(defun format:terpr nil
   (cond ((dtpr Format-Standard-Output)
	  (rplacd Format-Standard-Output
		  (cons #\newline (cdr Format-Standard-Output))))
	 (t (terpr Format-Standard-Output))))
(defun format:fresh-line nil
   (cond ((dtpr Format-Standard-Output)
	  (cond ((and (cdr Format-Standard-Output)
		      (not (= (cadr Format-Standard-Output) #\newline)))
		 (rplacd Format-Standard-Output
			 (cons #\newline (cdr Format-Standard-Output))))))
	 (t (and (not (= 0 (nwritn Format-Standard-Output)))
		 (terpr Format-Standard-Output)))))
(defun format\:string-search-char (char str start-pos)
       (declare (fixnum i start-pos str-len))
       (do ((i start-pos (1+ i))
	    (str-len (flatc str)))
	   ((>& i str-len) nil)
	   (and (=& char (getcharn str (1+ i))) (return i))))

)