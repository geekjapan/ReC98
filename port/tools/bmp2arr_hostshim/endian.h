/* macOS/host shim: assume little-endian host (x86_64/arm64) -- these are no-ops. */
#define le16toh(x) (x)
#define le32toh(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
