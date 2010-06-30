#get list of test directories
include tmp/*.name
TESTDIR = test_output

.PHONY: tests $(testlist)

tests: $(testlist)
	@printf "TESTS FINISHED - detailed results in the tests_output directory\n"
	@printf "===============================================================\n"
	@printf "|                   Results Summary:                          |\n"
	@printf "===============================================================\n"
	@tail -n 2 test_output/*

$(testlist):
	./pairCalcTest tmp/$@.config $(shell cat tmp/$@.dir) 1>$(TESTDIR)/$@.log 2>$(TESTDIR)/$@.out
