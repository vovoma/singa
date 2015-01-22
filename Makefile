###############################################################################
# External library Requirements for success compiling:
# 	gflags, glog, gtest, google-protobuf, mpich, boost, opencv.
###############################################################################
# Change this variable!! g++ location, should support c++11, tested with 4.8.1
HOME_DIR := /home/wangwei/install
# Location of g++
CXX := g++
# Header folder for system and external libs. You may need to change it.
INCLUDE_DIRS := $(HOME_DIR)/include $(HOME_DIR)/mpich/include ./include

CXXFLAGS := -g -Wall -pthread -fPIC -std=c++11 -Wno-unknown-pragmas \
	-funroll-loops $(foreach includedir, $(INCLUDE_DIRS), -I$(includedir))

MPI_LIBRARIES := mpicxx mpi
# Folder to store compiled files
LIBRARIES := $(MPI_LIBRARIES) glog gflags protobuf rt boost_system boost_regex \
							boost_thread boost_filesystem opencv_highgui opencv_imgproc\
							opencv_core openblas armci gtest
# Lib folder for system and external libs. You may need to change it.
LIBRARY_DIRS := $(HOME_DIR)/lib64 $(HOME_DIR)/lib $(HOME_DIR)/mpich/lib\

LDFLAGS := $(foreach librarydir, $(LIBRARY_DIRS), -L$(librarydir)) \
						$(foreach library, $(LIBRARIES), -l$(library)) $(MPI_LDFLAGS)

BUILD_DIR := build

###############################################################################
# Build singa into .a and .so library
###############################################################################
.PHONY: all proto init loader singa

# find user defined .proto file, and then compute the corresponding .h, .cc
# files, which cannot be found by shell find, because they haven't been
# generated currently
PROTOS := $(shell find src/proto/ -name "*.proto")
PROTO_SRCS :=$(PROTOS:.proto=.pb.cc)
PROTO_HDRS :=$(patsubst src%, include%, $(PROTOS:.proto=.pb.h))
PROTO_OBJS :=$(addprefix $(BUILD_DIR)/, $(PROTO_SRCS:.cc=.o))

# each singa src file will generate a .o file
SINGA_SRCS := $(shell find src/ \( -path "src/test" -o -path "src/main.cc" \) -prune \
	-o \( -name "*.cc" -type f \) -print )
SINGA_OBJS := $(sort $(addprefix $(BUILD_DIR)/, $(SINGA_SRCS:.cc=.o)) $(PROTO_OBJS) )
-include $(SINGA_OBJS:%.o=%.P)

LOADER_SRCS :=$(shell find tools/data_loader/ -name "*.cc") src/utils/shard.cc
LOADER_OBJS :=$(sort $(addprefix $(BUILD_DIR)/, $(LOADER_SRCS:.cc=.o)) $(PROTO_OBJS) )
-include $(LOADER_OBJS:%.o=%.P)

TEST_SRCS := src/test/test_mnistlayer.cc src/test/test_main.cc
TEST_OBJS := $(sort $(addprefix $(BUILD_DIR)/, $(TEST_SRCS:.cc=.o)) $(SINGA_OBJS))
-include $(TEST_OBJS:%.o=%.P)

OBJS := $(sort $(SINGA_OBJS) $(LOADER_OBJS) $(TEST_OBJS))

run: singa
	mpirun -np 2 -hostfile examples/mnist/hostfile ./build/singa \
	-cluster_conf=examples/mnist/cluster.conf -model_conf=examples/mnist/mlp.conf

singa: init proto  $(SINGA_OBJS)
	$(CXX) $(SINGA_OBJS) src/main.cc -o $(BUILD_DIR)/singa $(CXXFLAGS) $(LDFLAGS)
	@echo

loader: init proto $(LOADER_OBJS)
	$(CXX) $(LOADER_OBJS) -o $(BUILD_DIR)/loader $(CXXFLAGS) $(LDFLAGS)
	@echo

test: init proto $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o $(BUILD_DIR)/test $(CXXFLAGS) $(LDFLAGS)
	@echo


$(OBJS):$(BUILD_DIR)/%.o : %.cc
	$(CXX) $<  $(CXXFLAGS) -MMD -c -o $@
	cp $(BUILD_DIR)/$*.d $(BUILD_DIR)/$*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' < $(BUILD_DIR)/$*.d >> $(BUILD_DIR)/$*.P; \
	rm -f $*.d

# create folders
init:
	@ mkdir -p $(foreach obj, $(OBJS), $(dir $(obj)))
	@echo

proto: init $(PROTO_OBJS)

$(PROTO_HDRS) $(PROTO_SRCS): $(PROTOS)
	protoc --proto_path=src/proto --cpp_out=src/proto $(PROTOS)
	mkdir -p include/proto/
	cp src/proto/*.pb.h include/proto/
	@echo


###############################################################################
# Clean files generated by previous targets
###############################################################################
clean:
	rm -rf *.a *.so
	rm -rf include/proto/*
	rm -rf src/proto/*.pb.h src/proto/*.pb.cc
	rm -rf $(BUILD_DIR)
	@echo
