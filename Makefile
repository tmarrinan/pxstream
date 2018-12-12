############
MPICXX= mpic++
MPICXX_FLAGS= -std=c++11 -DASIO_STANDALONE -D_VARIADIC_MAX=10 -w
LIBCXX= ar
LIBCXX_FLAGS= rcs

NETSOCKET_DIR= $(HOME)/local
OPENSSL_DIR=/usr/local/opt/openssl
DDR_DIR= $(HOME)/Dev/ddr

# PX STREAM LIBRARY
INC= -I${NETSOCKET_DIR}/include -I$(OPENSSL_DIR)/include -I$(DDR_DIR)/include -I./include
SRCDIR= src
OBJDIR= obj
LIBDIR= lib
BINDIR= bin
OBJS= $(addprefix $(OBJDIR)/, pxstream.o server.o client.o)
HSLIB= $(addprefix $(LIBDIR)/, libpxstream.a)

# SAMPLE IMAGE STREAM SERVER
TEST_INC_S= -I${NETSOCKET_DIR}/include -I$(OPENSSL_DIR)/include -I./include -I./example/include
TEST_LIB_S= -L${NETSOCKET_DIR}/lib -L./lib -lnetsocket -lssl -lcrypto -lpthread -lpxstream
TEST_SRCDIR_S= example/src/server
TEST_OBJDIR_S= obj/server
TEST_OBJS_S= $(addprefix $(TEST_OBJDIR_S)/, main.o)
TEST_S= $(addprefix $(BINDIR)/, pxserver)

# SAMPLE IMAGE STREAM CLIENT
TEST_INC_C= -I${NETSOCKET_DIR}/include -I$(OPENSSL_DIR)/include -I$(DDR_DIR)/include -I./include -I./example/include
TEST_LIB_C= -L${NETSOCKET_DIR}/lib -L${DDR_DIR}/lib -L./lib -lnetsocket -ldl -lssl -lcrypto -lpthread -lpxstream -lddr
TEST_SRCDIR_C= example/src/client
TEST_OBJDIR_C= obj/client
TEST_OBJS_C= $(addprefix $(TEST_OBJDIR_C)/, main.o)
TEST_C= $(addprefix $(BINDIR)/, pxclient)

# SAMPLE IMAGE STREAM VIS CLIENT
TEST_INC_V= -I${NETSOCKET_DIR}/include -I$(OPENSSL_DIR)/include -I$(DDR_DIR)/include -I./include -I./example/include
TEST_LIB_V= -L${NETSOCKET_DIR}/lib -L${DDR_DIR}/lib -L./lib -lnetsocket -ldl -lssl -lcrypto -lglfw -lglad -lpthread -lpxstream -lddr
TEST_SRCDIR_V= example/src/vis
TEST_OBJDIR_V= obj/vis
TEST_OBJS_V= $(addprefix $(TEST_OBJDIR_V)/, main.o)
TEST_V= $(addprefix $(BINDIR)/, pxvis)

# CREATE DIRECTORIES (IF DON'T ALREADY EXIST)
mkdirs:= $(shell mkdir -p $(OBJDIR) $(TEST_OBJDIR_S) $(TEST_OBJDIR_C) $(TEST_OBJDIR_V) $(LIBDIR) $(BINDIR))

# BUILD EVERYTHING
all: $(HSLIB) $(TEST_S) $(TEST_C) $(TEST_V)

$(HSLIB): $(OBJS)
	$(LIBCXX) $(LIBCXX_FLAGS) $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(MPICXX) $(MPICXX_FLAGS) -c -o $@ $< $(INC)

$(TEST_S): $(TEST_OBJS_S)
	$(MPICXX) $(MPICXX_FLAGS) -o $@ $^ $(TEST_LIB_S)

$(TEST_OBJDIR_S)/%.o: $(TEST_SRCDIR_S)/%.cpp
	$(MPICXX) $(MPICXX_FLAGS) -c -o $@ $< $(TEST_INC_S)

$(TEST_C): $(TEST_OBJS_C)
	$(MPICXX) $(MPICXX_FLAGS) -o $@ $^ $(TEST_LIB_C)

$(TEST_OBJDIR_C)/%.o: $(TEST_SRCDIR_C)/%.cpp
	$(MPICXX) $(MPICXX_FLAGS) -c -o $@ $< $(TEST_INC_C)

$(TEST_V): $(TEST_OBJS_V)
	$(MPICXX) $(MPICXX_FLAGS) -o $@ $^ $(TEST_LIB_V)

$(TEST_OBJDIR_V)/%.o: $(TEST_SRCDIR_V)/%.cpp
	$(MPICXX) $(MPICXX_FLAGS) -c -o $@ $< $(TEST_INC_V)

# REMOVE OLD FILES
clean:
	rm -f $(OBJS) $(HSLIB) $(TEST_OBJS_S) $(TEST_OBJS_C) $(TEST_OBJS_V) $(TEST_S) $(TEST_C) $(TEST_V)
