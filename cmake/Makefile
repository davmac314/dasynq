all: Dasynq.cmake DasynqConfigVersion.cmake

install: all
	install -d "$(DESTDIR)$(LIBDIR)/cmake/Dasynq"
	install -m644 -t "$(DESTDIR)$(LIBDIR)/cmake/Dasynq" *.cmake

clean:
	rm -f Dasynq.cmake
	rm -f DasynqConfigVersion.cmake

Dasynq.cmake: Dasynq.cmake.m4
	m4 -D include_dir="\"$(HEADERDIR)\"" Dasynq.cmake.m4 > Dasynq.cmake

DasynqConfigVersion.cmake: DasynqConfigVersion.cmake.m4
	m4 -D dasynq_version=$(VERSION) -D dasynq_major=$(VERSION_MAJOR) DasynqConfigVersion.cmake.m4 > DasynqConfigVersion.cmake
