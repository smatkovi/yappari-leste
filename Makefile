# Helper targets for building on a PC. The Debian package itself is built
# with dpkg-buildpackage (debian/rules); it needs backend/yappari-wa first.

GO ?= go
# the phone's architecture: arm64 (Pro1X), or GOARCH=arm GOARM=7 for armhf
# (Nokia N950/N9)
GOARCH ?= arm64
GOARM ?=

.PHONY: backend test clean

# yappari-wa for the phone, statically linked, no cgo
backend:
	cd backend && GOOS=linux GOARCH=$(GOARCH) GOARM=$(GOARM) CGO_ENABLED=0 $(GO) build -trimpath -ldflags="-s -w" -o yappari-wa .

test:
	cd backend && $(GO) vet ./... && $(GO) test ./...

clean:
	rm -f backend/yappari-wa
