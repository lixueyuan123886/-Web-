CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp ./log/log.cpp 
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread

clean:
	rm  -r server 
	