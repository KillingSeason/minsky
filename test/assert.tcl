
proc assert {x args} {
    if {![expr $x]}  {
        puts stderr "assertion: $x failed: $args"
        minsky.resetEdited
        tcl_exit 1
    }
}
