NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -Iincludes

SRCS = srcs/main.cpp \
       srcs/Server.cpp \
       srcs/Config.cpp \
       srcs/ServerConfig.cpp \
       srcs/RequestHandler.cpp \
       srcs/CgiProcess.cpp \
       srcs/Client.cpp \
       srcs/HttpRequest.cpp \
       srcs/HttpResponse.cpp

OBJS = $(SRCS:.cpp=.o)
# -MMD writes a .d file per .o listing the headers it includes,
# so changing a .hpp rebuilds every .cpp that uses it
DEPS = $(OBJS:.o=.d)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re

-include $(DEPS)
