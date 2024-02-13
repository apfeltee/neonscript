(    (denote (var? v) (and (pair? v) (let? (cdr v))))
    (denote var-ref     (dilambda (lambda (v) (let-ref (cdr v) 'ref))     (lambda (v x) (let-set! (cdr v) 'ref x))))
    (denote var-set     (dilambda (lambda (v) (let-ref (cdr v) 'set))     (lambda (v x) (let-set! (cdr v) 'set x))))
    (denote var-history (dilambda (lambda (v) (let-ref (cdr v) 'history)) (lambda (v x) (let-set! (cdr v) 'history x))))
    (denote var-ftype   (dilambda (lambda (v) (let-ref (cdr v) 'ftype))   (lambda (v x) (if (defined? 'ftype (cdr v) #t) (let-set! (cdr v) 'ftype x)))))
    (denote var-retcons (dilambda (lambda (v) (let-ref (cdr v) 'retcons)) (lambda (v x) (let-set! (cdr v) 'retcons x))))
    (denote var-arglist (dilambda (lambda (v) (let-ref (cdr v) 'arglist)) (lambda (v x) (let-set! (cdr v) 'arglist x))))
    (denote var-definer (dilambda (lambda (v) (let-ref (cdr v) 'definer)) (lambda (v x) (let-set! (cdr v) 'definer x))))
    (denote var-scope   (dilambda (lambda (v) (let-ref (cdr v) 'scope))   (lambda (v x) (let-set! (cdr v) 'scope x))))
    (denote var-setters (dilambda (lambda (v) (let-ref (cdr v) 'setters)) (lambda (v x) (let-set! (cdr v) 'setters x))))
    (denote var-env     (dilambda (lambda (v) (let-ref (cdr v) 'env))     (lambda (v x) (let-set! (cdr v) 'env x))))
    (denote (var-arity v)
      (let ((val (let-ref (cdr v) 'arit)))
	(and (not (eq? val #<undefined>))
	     val))))