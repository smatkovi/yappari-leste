# Helper targets for building on a PC. The Debian package itself is built
# with dpkg-buildpackage (debian/rules); it needs backend/yappari-wa first.

GO ?= go

.PHONY: backend test clean

# yappari-wa for the phone (arm64), statically linked, no cgo
backend:
	cd backend && GOOS=linux GOARCH=arm64 CGO_ENABLED=0 $(GO) build -trimpath -ldflags="-s -w" -o yappari-wa .

test:
	cd backend && $(GO) vet ./... && $(GO) test ./...

clean:
	rm -f backend/yappari-wa
