.PHONY: copy check clean purge configure


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
		cp -R $$dir/* ns3/$$dir/ ;\
	done

configure: check
	ns3/ns3 configure

build: copy
	@echo "Building swarm simulation"
	ns3/ns3 build

clean:
	@rm -f *.o

purge: check clean
	ns3/ns3 clean
