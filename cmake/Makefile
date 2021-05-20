all: Dasynq.cmake DasynqConfigVersion.cmake

install: all
	install -d $(DESTDIR)$(LIBDIR)/cmake/Dasynq
	install -m644 *.cmake $(DESTDIR)$(LIBDIR)/cmake/Dasynq

clean:
	rm -f Dasynq.cmake
	rm -f DasynqConfigVersion.cmake

# Note we capture "CXXLINKOPTS" via the shell variable, not the make variable! this is so we don't lose
# shell escape & quoting characters (which, handily, also work for cmake).
Dasynq.cmake: Dasynq.cmake.m4
	m4 -D include_dir=$(HEADERDIR) -D link_libs="$$CXXLINKOPTS" Dasynq.cmake.m4 > Dasynq.cmake

DasynqConfigVersion.cmake: DasynqConfigVersion.cmake.m4
	m4 -D dasynq_version=$(VERSION) -D dasynq_major=$(VERSION_MAJOR) DasynqConfigVersion.cmake.m4 > DasynqConfigVersion.cmake
