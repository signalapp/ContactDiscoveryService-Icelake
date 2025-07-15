# Makefile for CDSI C library
# There's a shared Makefile.base that includes necessary flags for all subdirs
# There's a Makefile.subdirs that's used to build inside each subdir
# C code in subdirs is always compiled into a static libx_y.a, where x is the
# subdir, and y is the type of compilation (_t -> trusted, _u -> untrusted,
# _test -> testing).

include Makefile.base

RESOURCES_DIR ?= $(dir $(CURDIR))/src/main/resources/org/signal/cdsi/enclave
INSTALL_DIR ?= ../src/main/resources/org/signal/cdsi/enclave
AFL_FLAGS ?=
ADDITIONAL_CFLAGS ?=

doc:
	@echo Targets:
	@echo
	@echo = make all - build all binaries
	@echo = make install - build and add necessary artifacts to $$INSTALL_DIR
	@echo = make tests - build and run most tests
	@echo = make valgrinds - build and run most tests through valgrind
	@echo = make clean - remove all artifacts created by prior make
	@echo = make docker - build/install with Docker
	@echo
	@echo Running individual tests:
	@echo
	@echo = make path_oram/stash.test

all: enclave.bin testhost.bin libjnishim.so sign fuzz.bin enclave.testsigned $(TESTS) enclave.test

.PHONY: install
install: sign libjnishim.so
	mkdir -pv $(INSTALL_DIR)
	cp -fv enclave.Standard_DC2s_v3 $(INSTALL_DIR)/enclave-$(if $(ENCLAVE_ID),$(ENCLAVE_ID),Standard_DC2s_v3-$(shell $(OE_DIR)/bin/oesign dump -e enclave.Standard_DC2s_v3 | fgrep -i mrenclave | cut -d '=' -f2)).signed
	cp -fv libjnishim.so $(INSTALL_DIR)/libjnishim-$(if $(ENCLAVE_ID),$(ENCLAVE_ID),Standard_DC2s_v3-$(shell $(OE_DIR)/bin/oesign dump -e enclave.Standard_DC2s_v3 | fgrep -i mrenclave | cut -d '=' -f2)).so
	cp -fv enclave.Standard_DC24s_v3 $(INSTALL_DIR)/enclave-$(if $(ENCLAVE_ID),$(ENCLAVE_ID),Standard_DC24s_v3-$(shell $(OE_DIR)/bin/oesign dump -e enclave.Standard_DC24s_v3 | fgrep -i mrenclave | cut -d '=' -f2)).signed
	cp -fv libjnishim.so $(INSTALL_DIR)/libjnishim-$(if $(ENCLAVE_ID),$(ENCLAVE_ID),Standard_DC24s_v3-$(shell $(OE_DIR)/bin/oesign dump -e enclave.Standard_DC24s_v3 | fgrep -i mrenclave | cut -d '=' -f2)).so
	# If ENCLAVE_ID is specified, the -test enclave is the one that will be installed
	cp -fv enclave-debug.signed $(INSTALL_DIR)/enclave-$(if $(ENCLAVE_ID),$(ENCLAVE_ID),debug).signed
	cp -fv libjnishim.so $(INSTALL_DIR)/libjnishim-$(if $(ENCLAVE_ID),$(ENCLAVE_ID),debug).so

cdsi.h cdsi.c: ../src/main/proto/cdsi.proto
	cd proto && pbtools generate_c_source ../../src/main/proto/cdsi.proto

proto: cdsi.h cdsi.c

enclave: proto

LIBSODIUM_UNDEFS = \
  -UHAVE_MMAP \
  -UHAVE_MLOCK \
  -UHAVE_MADVISE \
  -UHAVE_MPROTECT \
  -UHAVE_RAISE \
##LIBSODIUM_UNDEFS

libsodium.a:
	rm -rf $$(pwd)/libsodium/.build && mkdir -p $$(pwd)/libsodium/.build
	(cd libsodium && (git clean -fxd || true) && ./configure \
  CFLAGS="$(TRUSTED_CFLAGS) $(LIBSODIUM_UNDEFS)" \
  CXXFLAGS="$(TRUSTED_CXXFLAGS) $(LIBSODIUM_UNDEFS)" \
  CC=$(CC) CXX=$(CXX) --prefix=$$(pwd)/.build && $(MAKE) clean && $(MAKE) -j install)
	cp -fv $$(pwd)/libsodium/.build/lib/libsodium.a $@

libnoise.a: libsodium.a
	mkdir -p $$(dirname $@)
	# Noise depends on the "immintrin" header which is not found in the pared-down
	# version of libc we use, and which it could otherwise not find with -nostdinc.
	(cd noise-c && \
	 (git clean -fxd ; git submodule foreach --recursive git clean -xfd ; true) && \
	 ./autogen.sh && \
   libsodium_CFLAGS=-I$$(pwd)/.libsodium/include/ libsodium_LIBS=libsodium.a \
	 CC=$(TRUSTED_CC) CFLAGS="$(TRUSTED_CFLAGS) -I$(shell ./find_header.sh $(CC) immintrin.h)" ./configure && \
	 $(MAKE) -j)
	cp -vf noise-c/src/protocol/libnoiseprotocol.a libnoise.a

libsip.a: SipHash/halfsiphash.c
	$(TRUSTED_CC) $(TRUSTED_CFLAGS) -o SipHash/halfsiphash.o -c SipHash/halfsiphash.c
	ar rcs $@ SipHash/halfsiphash.o

SUBDIRS=$(subst /,,$(wildcard */))
.PHONY: $(SUBDIRS)

lib%_t.a: % | gentrustuntrust
	$(MAKE) -C $* -f ../Makefile.subdirs libtrusted.a
	cp -v $*/libtrusted.a ./lib$*_t.a

lib%_u.a: % | gentrustuntrust
	$(MAKE) -C $* -f ../Makefile.subdirs libuntrusted.a
	cp -v $*/libuntrusted.a ./lib$*_u.a

lib%_test.a: % | gentrustuntrust
	$(MAKE) -C $* -f ../Makefile.subdirs libtest.a
	cp -v $*/libtest.a ./lib$*_test.a

lib%_afl.a: % | gentrustuntrust
	$(MAKE) -C $* -f ../Makefile.subdirs libafl.a
	cp -v $*/libafl.a ./lib$*_afl.a

untrust: gentrustuntrust
trust: gentrustuntrust

.PHONY: gentrustuntrust
gentrustuntrust: untrust/cds_args.h

untrust/cds_args.h: cds.edl
	mkdir -p trust untrust
	/opt/openenclave/bin/oeedger8r cds.edl \
	    --untrusted --untrusted-dir untrust \
	    --trusted --trusted-dir trust \
	    --search-path $(OE_DIR)/include \
	    --search-path $(OE_DIR)/include/openenclave/edl/sgx

TRUSTED_OE_LIBS=\
/opt/openenclave/lib/openenclave/enclave/liboeenclave.a \
/opt/openenclave/lib/openenclave/enclave/liboecryptombedtls.a \
/opt/openenclave/lib/openenclave/enclave/liboelibc.a \
/opt/openenclave/lib/openenclave/enclave/liboehostfs.a \
/opt/openenclave/lib/openenclave/enclave/libmbedtls.a \
/opt/openenclave/lib/openenclave/enclave/libmbedx509.a \
/opt/openenclave/lib/openenclave/enclave/libmbedcrypto.a \
/opt/openenclave/lib/openenclave/enclave/liboelibc.a \
/opt/openenclave/lib/openenclave/enclave/liboesyscall.a \
/opt/openenclave/lib/openenclave/enclave/liboecore.a \
##TRUSTED_OE_LIBS

TESTABLE_LIBRARY_DIRS= \
    proto \
    sharded_ohtable \
    ohtable \
    path_oram \
    ratelimit \
    noiseutil \
    util \
    fixedset \
    aci_pni_uak_helpers \
##TESTABLE_LIBRARY_DIRS

enclave.bin: \
    libtrust_t.a \
    libenclave_t.a \
    $(patsubst %,lib%_t.a,$(TESTABLE_LIBRARY_DIRS)) \
    libnoise.a \
    libsip.a \
    ##
	$(TRUSTED_CC) -o $@ $(TRUSTED_OE_LIBS) $^ $(TRUSTED_OE_LIBS) $(TRUSTED_LDFLAGS)

enclave.testbin: \
    libtrust_test.a \
    libenclave_test.a \
    $(patsubst %,lib%_test.a,$(TESTABLE_LIBRARY_DIRS)) \
    libnoise.a \
    libsip.a \
    ##
	$(TRUSTED_CC) -o $@ $(TRUSTED_OE_LIBS) $^ $(TRUSTED_OE_LIBS) $(TRUSTED_LDFLAGS)

fuzz.bin: \
    fuzz.c \
    libtrust_afl.a \
    libenclave_afl.a \
    $(patsubst %,lib%_afl.a,$(TESTABLE_LIBRARY_DIRS)) \
    libnoise.a \
    libsip.a \
    ##
	$(AFL_CC) -o $@ $(AFL_CFLAGS) $^ $(AFL_LDFLAGS)
 
.PHONY: fuzz
fuzz: fuzz.bin
	./aflfuzz.sh $(AFL_FLAGS)

UNTRUSTED_OE_LIBS=\
	/opt/openenclave/lib/openenclave/host/liboehost.a \
	/usr/lib/x86_64-linux-gnu/libcrypto.so \
##UNTRUSTED_OE_LIBS

libjnishim.so: \
    jnishim/jnishim.c \
    libutil_u.a \
    libuntrust_u.a \
    ##
	$(UNTRUSTED_CC) -shared -Wl,-soname,$@ -o $@ $^ $(UNTRUSTED_CFLAGS) $(UNTRUSTED_OE_LIBS) $(UNTRUSTED_LDFLAGS)

testhost.bin: \
    libtesthost_u.a \
    libproto_u.a \
    libuntrust_u.a \
    libnoise.a \
    libnoiseutil_u.a \
    libutil_u.a \
    ##
	$(UNTRUSTED_CC) -o $@ $^ $(UNTRUSTED_OE_LIBS) $(UNTRUSTED_LDFLAGS)

%.test: $(patsubst %,lib%_test.a,$(TESTABLE_LIBRARY_DIRS)) libsip.a libnoise.a
	$(TEST_CC) -o $@ $(dir $*)/tests/$(notdir $*).c $^ $(TEST_CFLAGS) $(TEST_LDFLAGS) libsip.a $(WARNING_CFLAGS)

%.test.out: %.test
	@echo ======================================================================
	@echo ==  START  $@ @ $$(date)
	@echo ======================================================================
	./$^ 2>&1 | tee $@
	@echo ==  FINISH $@ @ $$(date)
	@echo ======================================================================

%.test.valgrind: %.test
	@echo ======================================================================
	@echo ==  START  $@ @ $$(date)
	@echo ======================================================================
	valgrind --tool=memcheck --leak-check=full --error-exitcode=3 -s --exit-on-first-error=yes --undef-value-errors=no ./$^ 2>&1 | tee $@
	@echo ==  FINISH $@ @ $$(date)
	@echo ======================================================================

enclave.test: \
    libtesthost_test.a \
    libnoise.a \
    libproto_test.a \
    libuntrust_u.a \
    libnoiseutil_u.a \
    libutil_u.a
    ##
	$(TEST_CC) -o $@ testhost/tests/testhost.c  $(TEST_CFLAGS) $(TEST_LDFLAGS) \
    $^ $(UNTRUSTED_OE_LIBS) $(UNTRUSTED_LDFLAGS) $(WARNING_CFLAGS)

enclave.test.out: enclave.test enclave.testsigned
	@echo ======================================================================
	@echo ==  START  $@ @ $$(date)
	@echo ======================================================================
	./$< enclave.testsigned 2>&1 | tee $@
	@echo ==  FINISH $@ @ $$(date)
	@echo ======================================================================

TESTS=\
    fixedset/fixedset.test \
    aci_pni_uak_helpers/aci_pni_uak_helpers.test \
    util/util.test \
    path_oram/bucket.test \
    path_oram/path_oram.test \
    path_oram/position_map.test \
    path_oram/stash.test \
    path_oram/tree_path.test \
    ohtable/ohtable.test \
    sharded_ohtable/shard.test \
    sharded_ohtable/queue.test \
    ratelimit/ratelimit.test \
    noiseutil/noise.test \
    sharded_ohtable/sharded_ohtable.test \
##TESTS


tests: $(patsubst %,%.out,$(TESTS)) enclave.test.out constant_time_check.test
	@echo ALL TESTS SUCCEEDED
valgrinds: $(patsubst %,%.valgrind,$(TESTS))

public.pem:
	openssl genrsa -out private.pem -3 3072
	openssl rsa -in private.pem -pubout -out public.pem

.PHONY: sign
sign: enclave-debug.signed enclave.Standard_DC2s_v3 enclave.Standard_DC24s_v3

enclave-debug.signed: enclave.bin public.pem constant_time_check.test
	$(OE_DIR)/bin/oesign sign -e enclave.bin -c cds-test.conf -k private.pem -o $@

enclave.Standard_DC2s_v3: enclave.bin public.pem constant_time_check.test cds_Standard_DC2s_v3.conf
	$(OE_DIR)/bin/oesign sign -e enclave.bin -c cds_Standard_DC2s_v3.conf -k private.pem -o $@

enclave.Standard_DC24s_v3: enclave.bin public.pem constant_time_check.test cds_Standard_DC24s_v3.conf
	$(OE_DIR)/bin/oesign sign -e enclave.bin -c cds_Standard_DC24s_v3.conf -k private.pem -o $@

enclave.testsigned: enclave.testbin public.pem cds-test.conf
	$(OE_DIR)/bin/oesign sign -e enclave.testbin -c cds-test.conf -k private.pem -o $@

constant_time_check.test: constant_time_check.c
	$(TRUSTED_CC) -S -o constant_time_check.s constant_time_check.c $(TRUSTED_CFLAGS)
	if egrep '^\s*j' constant_time_check.s; then exit 1; fi # Look for jumps, which indicate non-constant timing

.PHONY: clean
clean:
	rm -rf *.a \
      constant_time_check.s \
      constant_time_check.test \
      testhost.bin \
      enclave.bin \
      enclave.testsigned \
      enclave.testbin \
      enclave.Standard_DC2s_v3 \
      enclave.Standard_DC24s_v3 \
      enclave-debug.signed \
      libjnishim.so \
      trust/ \
      untrust/ \
      private.pem \
      public.pem \
      enclave.test \
      enclave.test.out \
      proto/cdsi.c \
      proto/cdsi.h \
      libsodium/.build \
      ##
	for dir in $(SUBDIRS); do make -C $$dir -f ../Makefile.subdirs clean; done
	(cd noise-c && \
	 ($(MAKE) clean || true) && \
	 (git clean -fx ; git submodule foreach --recursive git clean -xfd  ; true)) >/dev/null
	@echo CLEAN

docker_%:
	mkdir -pv "$(RESOURCES_DIR)"
	docker buildx build --platform linux/amd64 -t cdsi-enclave-build ./docker --load $(DOCKER_BUILD_ARGS)
	docker run --rm -v "$(CURDIR)":/src -v "$(CURDIR)/../src/main":/src/main -v "$(RESOURCES_DIR)":/resources --user "$$(id -u):$$(id -g)" cdsi-enclave-build make ENCLAVE_ID=$(ENCLAVE_ID) INSTALL_DIR=/resources AFL_FLAGS="$(AFL_FLAGS)" ADDITIONAL_CFLAGS="$(ADDITIONAL_CFLAGS)" $*

dockersh:
	mkdir -pv "$(RESOURCES_DIR)"
	docker buildx build --platform linux/amd64 -t cdsi-enclave-build ./docker --load $(DOCKER_BUILD_ARGS)
	docker run --rm -it -v "$(CURDIR)":/src -v "$(CURDIR)/../src/main":/src/main -v "$(RESOURCES_DIR)":/resources --user "$$(id -u):$$(id -g)" cdsi-enclave-build 
