$clazyPlugin="ClazyClangTidy"

if($isWindows) {
    $clazyPlugin = $null
    #$clazy += ".dll" // don't use clazy on windows for now
} elseif($isLinux) {
    $clazyPlugin = "${env:KDEROOT}/lib/${clazyPlugin}.so"
} else
{
    $clazyPlugin = "${env:KDEROOT}/lib/${clazyPlugin}.dylib"
}
$CLAZY_LEVEL0="clazy-overloaded-signal,clazy-connect-by-name,clazy-connect-non-signal,clazy-qstring-comparison-to-implicit-char,clazy-wrong-qevent-cast,clazy-lambda-in-connect,clazy-lambda-unique-connection,clazy-qdatetime-utc,clazy-qgetenv,clazy-qstring-insensitive-allocation,clazy-fully-qualified-moc-types,clazy-unused-non-trivial-variable,clazy-connect-not-normalized,clazy-mutable-container-key,clazy-qenums,clazy-qmap-with-pointer-key,clazy-qstring-ref,clazy-strict-iterators,clazy-writing-to-temporary,clazy-container-anti-pattern,clazy-qcolor-from-literal,clazy-qfileinfo-exists,clazy-qstring-arg,clazy-empty-qstringliteral,clazy-qt-macros,clazy-temporary-iterator,clazy-wrong-qglobalstatic,clazy-lowercase-qml-type-name,clazy-no-module-include,clazy-use-static-qregularexpression"
$CLAZY_LEVEL1="clazy-auto-unexpected-qstringbuilder,clazy-connect-3arg-lambda,clazy-const-signal-or-slot,clazy-detaching-temporary,clazy-foreach,clazy-incorrect-emit,clazy-install-event-filter,clazy-non-pod-global-static,clazy-post-event,clazy-qdeleteall,clazy-qlatin1string-non-ascii,clazy-qproperty-without-notify,clazy-qstring-left,clazy-range-loop-detach,clazy-range-loop-reference,clazy-returning-data-from-temporary,clazy-rule-of-two-soft,clazy-child-event-qobject-cast,clazy-virtual-signal,clazy-overridden-signal,clazy-qhash-namespace,clazy-skipped-base-method,clazy-readlock-detaching"
$CLAZY_LEVEL2="clazy-ctor-missing-parent-argument,clazy-base-class-event,clazy-copyable-polymorphic,clazy-function-args-by-ref,clazy-function-args-by-value,clazy-global-const-char-pointer,clazy-implicit-casts,clazy-missing-qobject-macro,clazy-missing-typeinfo,clazy-old-style-connect,clazy-qstring-allocations,clazy-returning-void-expression,clazy-rule-of-three,clazy-virtual-call-ctor,clazy-static-pmf"

if ($clazyPlugin)
{
    $clazyCommand = @("-load=${clazyPlugin}", "-checks=${CLAZY_LEVEL0},-overloaded-signal,qt-keywords")
} else {
    $clazyCommand = @()
}

$clangCommand = $clazyCommand + @("-p",  "$env:BUILD_DIR")

run-clang-tidy @clangCommand | Tee-Object -Path "$([System.IO.Path]::GetTempPath())/clang-tidy.log"
