from functools import wraps

class _PPP_CB:
    '''
    Internal class to keep track of a the functions to run when a PPP-style
    callback is triggered
    '''
    def __init__(self):
        self.callbacks = []

    def run(self, *args):
        for targ_func in self.callbacks:
            targ_func(*args)

    def add_callback(self, fn):
        assert(fn not in self.callbacks), "Duplicate callback"
        self.callbacks.append(fn)

class PyPlugin:
    def __init__(self, panda):
        '''
        Base class which PyPANDA plugins should inherit. Subclasses may
        register callbacks using the provided panda object and use the
        PyPlugin APIs:

        * self.get_args or self.get_arg_bool to check argument values
        * self.ppp to interact with other PyPlugins via PPP interfaces
        * self.ppp_cb_boilerplate('cb_name') to register a ppp-style callback
        * self.ppp_run_cb('cb_name') to run a previously-registered ppp-style callback
        * @PyPlugin.ppp_export to mark a class method as ppp-exported

        For more information, check out the pyplugin documentation.
        '''

    # Parent init method which will be called prior to child __init__
    def __preinit__(self, pypluginmgr, args):
        self.ppp_cbs = {} # ppp callback name => _PPP instance which tracks registered cbs and runs them

        self.args = args
        self.pypluginmgr = pypluginmgr

    @property
    def ppp(self):
        # Why is this a property you ask? Because it makes it easier to set a docstring
        '''
        The .ppp property of the PyPlugin class is used for accessing PPP methods and callbacks
        exposed by other PyPlugins. (Under the hood, this is a refernece to the PyPluginManager.ppp
        property).

        Through self.ppp, you can reference another PyPlugin by name, e.g., if a previously-loaded plugin
        is named `Server`, from your plugin you can do `self.ppp.Server` to access PPP-exported methods.

        From there, you can run PPP-exported functions by name: `self.ppp.Server.some_exported_fn(*args)`.
        Or you can register a local class method a PPP-style callback provided by the other plugin:
            `self.ppp.server.ppp_reg_cb('some_provided_callback', self.some_local_method)`
        '''
        return self.pypluginmgr.ppp


    @staticmethod
    def ppp_export(method):
        '''
        Decorator to apply to a class method in a PyPlugin to indicate that other plugins should
        be allowed to call this function. Example:

            from pandare import PyPlugin
            Class Server(PyPlugin):
                def __init__(self, panda):
                    pass

                @PyPlugin.ppp_export
                def do_add(self, x):
                    return x+1

            Class Client(PyPlugin):
                def __init__(self, panda):
                    print(self.ppp.Server.do_add(1))
        '''
        @wraps(method)
        def f(*args, **kwargs):
            return method(*args, **kwargs)
        f.__is_pyplugin_ppp = True
        f.__original_method = method
        return f

    # Argument loading
    def get_arg(self, arg_name):
        '''
        Returns either the argument as a string or None if the argument
        wasn't passed (arguments passed in bool form (i.e., set but with no value)
        instead of key/value form will also return None).
        '''
        if arg_name in self.args:
            return self.args[arg_name]

        return None

    def get_arg_bool(self, arg_name):
        '''
        Returns True if the argument is set and has a truthy value
        '''

        if arg_name not in self.args:
            # Argument name unset - it's false
            return False

        arg_val = self.args[arg_name]
        if isinstance(arg_val, bool):
            # If it's a python bol already, just return it
            return arg_val

        if isinstance(arg_val, str):
            # string of true/y/1  is True
            return arg_val.lower() in ['true', 'y', '1']

        if isinstance(arg_val, int):
            # Nonzero is True
            return arg_val != 0

        # If it's not a string, int, or bool something is weird
        raise ValueError(f"Unsupported arg type: {type(arg_val)}")

    # Callback definition / registration / use. Note these functions mirror the behavior of the macros used
    # in C plugin, check out docs/readme.md for additional details.

    def ppp_cb_boilerplate(self, cb_name):
        '''
        "Define" a PPP-style function in this plugin. Note that there is no type
        information because this is Python. Run via .ppp[cb_name].run(...)
        '''
        plugin_name = self.__class__.__name__

        if cb_name in self.ppp_cbs:
            raise ValueError(f"PPP function {cb_name} is being redefined in {plugin_name}")

        # Add two callbacks into our PPP namesapce: fn_add and fn_run
        this_ppp_cb = _PPP_CB()
        self.ppp.add(self.__class__.__name__, "ppp_reg_cb_" + cb_name, this_ppp_cb.add_callback)
        self.ppp.add(self.__class__.__name__, "ppp_run_cb_" + cb_name, this_ppp_cb.run)

        # Make sure we have a helper self.ppp[class].ppp_reg_cb which just calls
        # the ppp_reg_[cb_name] we just saved
        try:
            getattr(getattr(self.ppp, self.__class__.__name__), "ppp_reg_cb")
        except AttributeError:
            def _reg_cb(target_ppp, func):
                getattr(getattr(self.ppp,
                    self.__class__.__name__), "ppp_reg_cb_" + target_ppp)(func)
            self.ppp.add(self.__class__.__name__, "ppp_reg_cb", _reg_cb)

    def ppp_run_cb(self, target_ppp, *args):
        '''
        Trigger a previously defind PPP-style callback named `target_ppp` in this plugin with `args`
        Any other pyplugins which have registered a function to run on this callback will be called with `args`.
        '''
        getattr(getattr(self.ppp, self.__class__.__name__), "ppp_run_cb_" + target_ppp)(*args)
