VERSION := v0.0.8

.PHONY: resin stamina charge deskband all clean login

ENV := CGO_ENABLED=1 GOOS=windows GOARCH=amd64
LDFLAGS := -ldflags "-H=windowsgui"
DESKBAND_LIBS := -lole32 -loleaut32 -luuid -lshlwapi -lwinhttp -lcomctl32 -luxtheme -lgdi32 -ladvapi32

all: resin stamina charge

resin:
	${ENV} go build $(LDFLAGS) -o resin.exe cmd/resin/main.go

stamina:
	${ENV} go build $(LDFLAGS) -o stamina.exe cmd/stamina/main.go

charge:
	${ENV} go build $(LDFLAGS) -o charge.exe cmd/charge/main.go

deskband:
	g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -o RealTimeNotesDeskband.dll deskband/RealTimeNotesDeskband.cpp $(DESKBAND_LIBS)

login:
	VERSION=$(VERSION) ./buildLogin

clean:
	rm -rf resin*.exe
	rm -rf stamina*.exe
	rm -rf charge*.exe
	rm -rf RealTimeNotesDeskband.dll
	rm -rf login/*.exe.WebView2
