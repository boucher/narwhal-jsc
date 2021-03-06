#define NOTRACE

#include <JavaScriptCore/JavaScriptCore.h>
#include <stdio.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <narwhal.h>

#include <dlfcn.h>

#ifdef JSCOCOA
#import <JSCocoa/JSCocoa.h>
#endif

#define HANDLE_EXCEPTION(shouldPrint, shouldReturn) \
    if (*_exception) { \
        if (shouldPrint) JSValuePrint(_context, NULL, *_exception); \
        if (shouldReturn) return NULL; \
    } \

void JSValuePrint(JSContextRef _context, JSValueRef *_exception, JSValueRef value)
{
    if (!value) return;
    
    JSStringRef string = JSValueToStringCopy(_context, value, _exception);
    if (!string || _exception && *_exception)
        return;
    
    size_t length = JSStringGetMaximumUTF8CStringSize(string);
    
    char *buffer = (char*)malloc(length);
    if (!buffer) {
        if (_exception)
            *_exception = JSValueMakeStringWithUTF8CString(_context, "print error (malloc)");
        JSStringRelease(string);
        return;
    }
    
    JSStringGetUTF8CString(string, buffer, length);
    JSStringRelease(string);
    
    puts(buffer);
    free(buffer);
}


// Reads a file into a v8 string.
JSStringRef ReadFile(const char* name) {
    FILE* file = fopen(name, "rb");
    if (file == NULL) return NULL;

    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    rewind(file);

    char *chars = (char*)malloc(sizeof(char) * (size + 1));
    int i;
    for (i = 0; i < size;) {
        int read = fread(&chars[i], 1, size - i, file);
        i += read;
    }
    chars[size] = '\0';
    fclose(file);
    JSStringRef result = JSStringCreateWithUTF8CString(chars);
    free(chars);
    return result;
}

FUNCTION(Print)
{
    size_t i;
    for (i = 0; i < ARGC; i++) {
        JSValuePrint(_context, _exception, ARGV(i));
        if (_exception)
            return NULL;
    }
        
    return JS_undefined;
}
END

FUNCTION(Read, ARG_UTF8(path))
{
    ARG_COUNT(1);

    JSStringRef contents = ReadFile(path);

    if (contents) {
        JSValueRef result = JSValueMakeString(_context, contents);
        JSStringRelease(contents);
        
        if (result)
            return result;
    }
    
    *_exception = JSValueMakeStringWithUTF8CString(_context, "read() error occurred");
    
    return JS_undefined;
}
END

FUNCTION(IsFile, ARG_UTF8(path))
{
    ARG_COUNT(1);

    struct stat stat_info;
    int ret = stat(path, &stat_info);

    return JS_bool(ret != -1 && S_ISREG(stat_info.st_mode));
}
END

typedef JSObjectCallAsFunctionCallback factory_t;
typedef const char *(*getModuleName_t)(void);

FUNCTION(RequireNative, ARG_UTF8(topId), ARG_UTF8(path))
{
    ARG_COUNT(2)
    
    DEBUG("RequireNative: topId=[%s] path=[%s] \n", topId, path);
    
    void *handle = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
    if (handle == NULL) {
        printf("dlopen error: %s\n", dlerror());
        return JS_null;
    }
    
    getModuleName_t getModuleName = (getModuleName_t)dlsym(handle, "getModuleName");
    if (getModuleName == NULL) {
        fprintf(stderr, "dlsym (getModuleName) error: %s\n", dlerror());
        return JS_null;
    }
    
    factory_t func = (factory_t)dlsym(handle, getModuleName());
    if (func == NULL) {
        fprintf(stderr, "dlsym (%s) error: %s\n", getModuleName(), dlerror());
        return JS_null;
    }
    
    return JS_fn(func);
}
END

JSObjectRef envpToObject(JSGlobalContextRef _context, JSValueRef *_exception, char *envp[])
{
    JSObjectRef ENV = JSObjectMake(_context, NULL, NULL);
    
    char *key, *value;
    while (key = *(envp++)) {
        value = strchr(key, '=') + 1;
        *(value - 1) = '\0';
        
        SET_VALUE(ENV, key, JS_str_utf8(value, strlen(value)));
        HANDLE_EXCEPTION(true, true);
        
        *(value - 1) = '=';
    }
    
    return ENV;
}

JSObjectRef argvToArray(JSGlobalContextRef _context, JSValueRef *_exception, int argc, char *argv[])
{
    JSValueRef arguments[argc];
    
    int i;
    for (i = 0; i < argc; i++)
        arguments[i] = JSValueMakeStringWithUTF8CString(_context, argv[i]);
    
    JSObjectRef ARGS = JSObjectMakeArray(_context, argc, arguments, _exception);
    HANDLE_EXCEPTION(true, true);
    
    return ARGS;
}

// The read-eval-execute loop of the shell.
void* RunShell(JSContextRef _context) {
    printf("Narwhal version %s, JavaScriptCore version %s\n", "0.1", "FOO");
    while (true)
    {
#ifdef JSCOCOA
        NSAutoreleasePool *pool = [NSAutoreleasePool new];
#endif
        char *str = readline("> ");
        if (str && *str)
            add_history(str);
        
        if (str == NULL) break;
        
        JSStringRef source = JSStringCreateWithUTF8CString(str);
        free(str);
        
        JSValueRef exception = NULL;
        JSValueRef *_exception = &exception;
        
        if (JSCheckScriptSyntax(_context, source, 0, 0, _exception) && !(*_exception))
        {
            JSValueRef value = JSEvaluateScript(_context, source, 0, 0, 0, _exception);
            HANDLE_EXCEPTION(true, false);
            
            if (value && !JSValueIsUndefined(_context, value)) {
                JSValuePrint(_context, _exception, value);
                HANDLE_EXCEPTION(true, false);
            }
        }
        else
        {
            HANDLE_EXCEPTION(true, false);
        }
        
        JSStringRelease(source);

#ifdef JSCOCOA
        [pool drain];
#endif
    }
    printf("\n");
}

int narwhal(int argc, char *argv[], char *envp[])
{
#ifdef JSCOCOA
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    JSCocoaController* jsc = [JSCocoa new];
    JSGlobalContextRef _context = [jsc ctx];
#else
    JSGlobalContextRef _context = JSGlobalContextCreate(NULL);
#endif

    JSObjectRef global = JS_GLOBAL;
    
    JSValueRef exception = NULL;
    JSValueRef *_exception = &exception;

    SET_VALUE(global, "print",          JS_fn(Print));
    SET_VALUE(global, "isFile",         JS_fn(IsFile));
    SET_VALUE(global, "read",           JS_fn(Read));
    SET_VALUE(global, "requireNative",  JS_fn(RequireNative));
    SET_VALUE(global, "ARGS",           argvToArray(_context, _exception, argc, argv));
    SET_VALUE(global, "ENV",            envpToObject(_context, _exception, envp));
    
    // Load bootstrap.js
    char *NARWHAL_ENGINE_HOME = getenv("NARWHAL_ENGINE_HOME");
    char *bootstrapPathRelative = "/bootstrap.js";
    char bootstrapPathFull[strlen(NARWHAL_ENGINE_HOME) + strlen(bootstrapPathRelative) + 1];
    snprintf(bootstrapPathFull, sizeof(bootstrapPathFull), "%s%s", NARWHAL_ENGINE_HOME, bootstrapPathRelative);
    
    JSStringRef bootstrapSource = ReadFile(bootstrapPathFull);
    if (!bootstrapSource) {
        printf("Error reading bootstrap.js\n");
        return 1;
    }
    JSEvaluateScript(_context, bootstrapSource, 0, 0, 0, _exception);
    JSStringRelease(bootstrapSource);
    HANDLE_EXCEPTION(true, true);
    
    RunShell(_context);

#ifdef JSCOCOA
    [pool drain];
#endif
}

int main(int argc, char *argv[], char *envp[])
{
    return narwhal(argc, argv, envp);
}
