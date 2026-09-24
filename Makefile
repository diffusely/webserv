NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -Iincludes

SRCS = srcs/main.cpp \
       srcs/Server.cpp \
       srcs/Config.cpp \
       srcs/ServerConfig.cpp \
       srcs/RequestHandler.cpp \
       srcs/Client.cpp \
       srcs/HttpRequest.cpp \
       srcs/HttpResponse.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
