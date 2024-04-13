.PHONY: copy check clean purge configure test

# To speedup builds, we only enable what we need
INCLUDE_MODULES_MIN = point-to-point;applications;wcmp;internet;flow-monitor
INCLUDE_MODULES_MPI = $(INCLUDE_MODULES_MIN);mpi
INCLUDE_MODULES_NETANIM = $(INCLUDE_MODULES_MIN);netanim
INCLUDE_MODULES_ALL = $(INCLUDE_MODULES_MIN);netanim;mpi

TEST_MODULES = wcmp

DIRS = src scratch

check:
	@echo "Checking for the ns3 symlink"
	@if [ ! -L ns3 ]; then\
		echo "Did not find the symlink";\
		echo "You must link 'ns3' to an existing ns3 distribution";\
		echo "\n\tln -s <path to ns3 distribution> ns3\n";\
		exit 0;\
	fi

copy: check
	@for dir in $(DIRS); do \
		rsync -av $$dir/ ns3/$$dir/ ;\
	done
	OUTPUT_DIR=$(shell readlink ns3)/build
	@echo $(OUTPUT_DIR)

configure-all: copy
	@echo "Configuring with MPI and NETANIM"
	ns3/ns3 configure --enable-tests --enable-mpi --enable-modules $(INCLUDE_MODULES_ALL) --filter-module-examples-and-tests $(TEST_MODULES) --out $(OUTPUT_DIR)

configure-mpi: copy
	@echo "Configuring with MPI"
	ns3/ns3 configure --enable-mpi --enable-tests --enable-modules $(INCLUDE_MODULES_MPI) --filter-module-examples-and-tests "mpi;$(TEST_MODULES)" --out $(OUTPUT_DIR)

configure-netanim: copy
	@echo "Configuring with NETANIM"
	ns3/ns3 configure --enable-tests --enable-modules $(INCLUDE_MODULES_NETANIM) --filter-module-examples-and-tests $(TEST_MODULES) --out $(OUTPUT_DIR)

configure-min: copy
	@echo "Configuring with minimum dependencies"
	ns3/ns3 configure --enable-tests --enable-modules $(INCLUDE_MODULES_MIN) --filter-module-examples-and-tests $(TEST_MODULES) --out $(OUTPUT_DIR)

configure-opt: copy
	@echo "Configuring optimized build"
	ns3/ns3 configure -d optimized --enable-mpi --enable-modules $(INCLUDE_MODULES_MPI) --out $(OUTPUT_DIR)

build: copy
	@echo "Building swarm simulation"
	ns3/ns3

clean:
	@echo "Cleaning current results and outputs"
	@rm -f *.o
	@rm -f ns3/*.pcap
	@rm -f ns3/*.xml
	@rm -f ns3/*.routes

purge: check clean
	@echo "Purging current distribution"
	@cd $(readlink ns3); ./ns3 distclean; \

