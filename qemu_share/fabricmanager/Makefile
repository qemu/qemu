CXX = g++
CXXFLAGS = -Wall -Wextra -g -O3 -std=c++17
LDFLAGS = 

OBJFILES_FM = cxl_fm.o memdevice.o
TARGET_FM = cxl_fm

OBJFILES_ADMIN = cxl_server_admin.o
TARGET_ADMIN = cxl_server_admin

all: $(TARGET_FM) $(TARGET_ADMIN)

$(TARGET_FM): $(OBJFILES_FM)
	$(CXX) $(CXXFLAGS) -o $(TARGET_FM) $(OBJFILES_FM) $(LDFLAGS)

$(TARGET_ADMIN): $(OBJFILES_ADMIN)
	$(CXX) $(CXXFLAGS) -o $(TARGET_ADMIN) $(OBJFILES_ADMIN) $(LDFLAGS)

clean:
	rm -f $(OBJFILES_FM) $(TARGET_FM) $(TARGET_ADMIN) $(OBJFILES_ADMIN)