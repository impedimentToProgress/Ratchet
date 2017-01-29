
.PHONY: all_libs
all_libs:
	cd builtins; make; cd -
	cd libc; rm -r build/; ./run.sh; cd -


clean:
	cd builtins; make clean; cd -
	cd libc/build; make clean; cd -

install:
	cd builtins; make install; cd -
	cd libc/build; make install; cd -

