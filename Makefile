.PHONY: copy check clean purge configure test

# To speedup builds, we only enable what we need
INCLUDE_MODULES_MIN = point-to-point;applications;wcmp;internet;flow-monitor
INCLUDE_MODULES_MPI = $(INCLUDE_MODULES_MIN);mpi;flowmon-mpi
INCLUDE_MODULES_NETANIM = $(INCLUDE_MODULES_MIN);netanim
INCLUDE_MODULES_ALL = $(INCLUDE_MODULES_MIN);netanim;mpi;flowmon-mpi

TEST_MODULES = wcmp

DIRS = src scratch
NS3_DIR=$(shell readlink ns3)

check:
	$(info Checking for the ns3 symlink)
	@if [ ! -L ns3 ]; then\
		echo "Did not find the symlink";\
		echo "You must link 'ns3' to an existing ns3 distribution";\
		echo "\n\tln -s <path to ns3 distribution> ns3\n";\
		exit 0;\
	fi
	$(info NS3 absolute path is ${NS3_DIR})

copy: check
	@for dir in $(DIRS); do \
		rsync -av $$dir/ ns3/$$dir/ ;\
	done

configure-all: copy
	$(info Configuring with MPI and NETANIM)
	@ cd ${NS3_DIR}; \
	./ns3 configure --enable-tests --enable-mpi --enable-modules "${INCLUDE_MODULES_ALL}" --filter-module-examples-and-tests "${TEST_MODULES}" \

configure-mpi: copy
	$(info Configuring with MPI)
	@ cd ${NS3_DIR}; \
	export CMAKE_CXX_FLAGS="-DNETANIM_ENABLED=0"; \
	./ns3 configure --enable-mpi --enable-tests --enable-modules "${INCLUDE_MODULES_MPI}" --filter-module-examples-and-tests "mpi;${TEST_MODULES}" \

configure-netanim: copy
	$(info Configuring with NETANIM)
	@ cd ${NS3_DIR}; \
	export CMAKE_CXX_FLAGS="-DMPI_ENABLED=0"; \
	./ns3 configure --enable-tests --enable-modules "${INCLUDE_MODULES_NETANIM}" --filter-module-examples-and-tests "${TEST_MODULES}" \

configure-min: copy
	$(info Configuring with minimum dependencies)
	@ cd ${NS3_DIR}; \
	export CMAKE_CXX_FLAGS="-DMPI_ENABLED=0 -DNETANIM_ENABLED=0"; \
	./ns3 configure --enable-tests --enable-modules "${INCLUDE_MODULES_MIN}" --filter-module-examples-and-tests "${TEST_MODULES}"; \
	./ns3 build \

configure-opt: copy
	$(info Configuring optimized build)
	@ cd ${NS3_DIR}; \
	export CMAKE_CXX_FLAGS="-DNETANIM_ENABLED=0"; \
	./ns3 configure -d optimized --enable-mpi --enable-modules "${INCLUDE_MODULES_MPI}" \

build: copy
	$(info Building swarm simulation)
	@ cd ${NS3_DIR}; \
	./ns3 build \

clean:
	$(info Cleaning current results and outputs)
	@rm -f *.o
	@rm -f ns3/*.pcap
	@rm -f ns3/*.xml
	@rm -f ns3/*.routes
	@rm -f ns3/swarm-pcaps/*.pcap

purge: check clean
	$(info Purging current distribution)
	@ cd ${NS3_DIR}; \
	./ns3 distclean \

