CXX ?= g++

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient -w

clean:
	if  [ -f  "server"  ]; then rm server; fi

# 清理server程序和日志
cleanall:
	if  [ -f  "server"  ]; then rm server; fi
	find . -name "*_ServerLog" | xargs rm -rf