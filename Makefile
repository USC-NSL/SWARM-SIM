.PHONY: copy check clean purge configure test

# To speedup builds, we only enable what we need (Using MPI means no NetAnim)
INCLUDE_MODULES = "point-to-point;applications;wcmp;internet;netanim;flow-monitor"
INCLUDE_MODULES_MPI = "point-to-point;applications;wcmp;internet;flow-monitor;mpi"

TEST_MODULES = "wcmp"

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
		rsync -av $$dir/* ns3/$$dir/ ;\
	done

configure: copy
	ns3/ns3 configure --enable-tests --enable-modules $(INCLUDE_MODULES) --filter-module-examples-and-tests $(TEST_MODULES)

# Note: Using MPI disables log messages, since it uses the optmized NS-3 build
configure-mpi:
	ns3/ns3 configure --enable-mpi --enable-examples --enable-modules $(INCLUDE_MODULES_MPI) -d optimized --filter-module-examples-and-tests mpi

build: copy
	@echo "Building swarm simulation"
	ns3/ns3

clean:
	@rm -f *.o
	@rm -f ns3/*.pcap
	@rm -f ns3/*.xml
	@rm -f ns3/*.routes

purge: check clean
	ns3/ns3 distclean

