#!/bin/sh
#
# Build and test Git
#

. ${0%/*}/lib.sh

case "$CI_OS_NAME" in
windows*) cmd //c mklink //j t\\.prove "$(cygpath -aw "$cache_dir/.prove")";;
*) ln -s "$cache_dir/.prove" t/.prove;;
esac

run_tests=t

case "$jobname" in
linux-gcc)
	export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
	;;
linux-TEST-vars)
	export GIT_TEST_SPLIT_INDEX=yes
	export GIT_TEST_MERGE_ALGORITHM=recursive
	export GIT_TEST_FULL_IN_PACK_ARRAY=true
	export GIT_TEST_OE_SIZE=10
	export GIT_TEST_OE_DELTA_SIZE=5
	export GIT_TEST_COMMIT_GRAPH=1
	export GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS=1
	export GIT_TEST_MULTI_PACK_INDEX=1
	export GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=1
	export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
	export GIT_TEST_NO_WRITE_REV_INDEX=1
	export GIT_TEST_CHECKOUT_WORKERS=2
<<<<<<< HEAD
	export GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL=1
=======
	export GIT_TEST_PACKED_REFS_VERSION=2
>>>>>>> origin/jch
	;;
linux-clang)
	export GIT_TEST_DEFAULT_HASH=sha1
	;;
linux-sha256)
	export GIT_TEST_DEFAULT_HASH=sha256
	;;
pedantic)
	# Don't run the tests; we only care about whether Git can be
	# built.
	export DEVOPTS=pedantic
	run_tests=
	;;
esac

mc=
if test "$jobname" = "linux-cmake-ctest"
then
	cb=contrib/buildsystems
	group CMake cmake -S "$cb" -B "$cb/out"
	mc="-C $cb/out"
fi

group Build make $mc

if test -n "$run_tests"
then
	group "Run tests" make $mc test ||
	handle_failed_tests
	group "Run unit tests" \
		make DEFAULT_UNIT_TEST_TARGET=unit-tests-prove unit-tests
fi
check_unignored_build_artifacts

save_good_tree
