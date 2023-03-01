CXX ?= g++
DEBUG ?= 0
CXXFLAGS = -std=c++11
NO_WARNINGS ?= 0

ifeq ($(NO_WARNINGS), 1)
	CXXFLAGS += -w
endif

ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    # CXXFLAGS += -O2

endif

server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./mysql/sql_connection_pool.cpp  webserver.cpp config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	if  [ -f  "server"  ]; then rm server; fi

# 清理server程序和日志
cleanall:
	if  [ -f  "server"  ]; then rm server; fi
	find . -name "*_ServerLog" | xargs rm -rf