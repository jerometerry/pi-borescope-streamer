### KUnit (The Kernel Equivalent of Google Test)

```c
#include <kunit/test.h>
#include "useeplus.h"

static void test_up_has_gravity_sensor(struct kunit *test)
{
        KUNIT_EXPECT_TRUE(test, up_has_gravity_sensor(0x01));
        KUNIT_EXPECT_FALSE(test, up_has_gravity_sensor(0x00));
}

static void test_up_parse_envelope_bounds(struct kunit *test)
{
        struct up_drv_data drv_data = {0};
        struct up_parse_ctx ctx = { .index = 0 };
        struct up_envelope env;

        /* Setup mock buffer that is too small */
        drv_data.decode_buf_len = 4;

        KUNIT_EXPECT_EQ(test, up_parse_envelope(&drv_data, &ctx, &env), UP_PARSE_NEED_DATA);
}

static struct kunit_case up_test_cases[] = {
        KUNIT_CASE(test_up_has_gravity_sensor),
        KUNIT_CASE(test_up_parse_envelope_bounds),
        {}
};

static struct kunit_suite up_test_suite = {
        .name = "useeplus_parser",
        .test_cases = up_test_cases,
};
kunit_test_suite(up_test_suite);

```

### User-Space Extraction (The Protocol Stubbing Strategy)

You mentioned wanting to extract the logic into a library designed for testing. Kernel developers often do exactly this for complex parsers or math-heavy subsystems so they can leverage user-space fuzzers and tools like `valgrind` or standard Google Test.

Create a mock `linux_stub.h` header to replace the kernel macros:

```c
/* linux_stub.h - Mocking kernel macros for user-space GTest */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define likely(x)   (x)
#define unlikely(x) (x)
#define __packed    __attribute__((packed))
#define le16_to_cpu(x) (x) /* Assuming you test on a little-endian host */

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t __le16;
typedef uint32_t __le32;

```

If you `#include` a stub like that conditionally when compiling for tests, you can drag `useeplus.c` straight into your existing CMake and Google Test pipeline in the `pi-borescope-streamer` repository, allowing you to test the parsing logic entirely outside the kernel.

### `v4l2-compliance`

The Linux Media subsystem maintains a rigorous userspace tool called **`v4l2-compliance`**. It forcefully tests every single IOCTL your driver exposes, attempts to feed it malformed V4L2 structures, tests buffer queuing/dequeuing under stress, and yells at you if you violate the V4L2 specification.

Running this against your `video0` device is a rite of passage for V4L2 drivers:

```bash
v4l2-compliance -d /dev/video0

```

### 4. `kselftest` (Kernel Selftests)

For broader integration testing, the kernel tree includes the `tools/testing/selftests/` directory. This is where developers write bash scripts or C programs that run in user-space to execute end-to-end tests against their drivers (like opening the device, requesting a stream, simulating a disconnect, and checking if the module unloads cleanly without a memory leak).

### The Best Path Forward

If you want to stay in the C++ / CMake ecosystem you already have set up for the MJPEG server, **User-Space Extraction** with a mocked kernel header is a fantastic, highly productive way to get 100% test coverage on your parser.

If you want to do it "The Linux Way" using the tools built into the kernel tree, **KUnit** is the exact equivalent you are looking for.
