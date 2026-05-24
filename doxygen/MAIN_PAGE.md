This provides a basic source-code generated documentation for the core classes of the lumex-node.
Doxygen docs may look a bit overwhelming as it tries to document all the smaller pieces of code. For
this reason only the files from `lumex` directory were added to this. Some other
files were also excluded as the `EXCLUDE_PATTERN` configuration stated below.

    EXCLUDE_PATTERNS       = */lumex/*_test/* \
                             */lumex/test_common/* \
                             */lumex/boost/* \
                             */lumex/qt/* \
                             */lumex/lumex_wallet/*

