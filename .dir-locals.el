((c-mode . ((c-file-style . "stroustrup")
	    (indent-tabs-mode . nil)))
 (python-mode .
              ((eval . (when-let* ((root (locate-dominating-file default-directory ".dir-locals.el")))
                         (setq-local process-environment (copy-sequence process-environment))
                         (setenv "PYTHONPATH"
                                 (concat (expand-file-name "python" root) ":"
                                         (expand-file-name "tests/functional" root))))))))
