# CMake generated Testfile for 
# Source directory: C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests
# Build directory: C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit_tests]=] "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/build/tests/run_tests.exe")
set_tests_properties([=[unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;26;add_test;C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;0;")
add_test([=[parser_tests]=] "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/build/tests/test_parser.exe")
set_tests_properties([=[parser_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;27;add_test;C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;0;")
add_test([=[catalog_tests]=] "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/build/tests/test_catalog.exe")
set_tests_properties([=[catalog_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;28;add_test;C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;0;")
add_test([=[optimizer_tests]=] "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/build/tests/test_optimizer.exe")
set_tests_properties([=[optimizer_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;29;add_test;C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;0;")
add_test([=[cbo_tests]=] "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/build/tests/test_cbo.exe")
set_tests_properties([=[cbo_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;30;add_test;C:/Users/ghosh/Downloads/OptiCore_Query_Optimizer-main (1)/OptiCore_Query_Optimizer-main/tests/CMakeLists.txt;0;")
