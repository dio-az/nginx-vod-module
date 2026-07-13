#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "ngx_http_vod_hls.c"

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    // Invariant: Buffer reads never exceed the declared length
    const char *payloads[] = {
        "A",  // Valid input (boundary case - minimal)
        "1234567890123456789012345678901234567890",  // Normal input
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",  // 256 chars - likely exceeds buffer
        "EXPLOIT_PAYLOAD_VERY_LONG_SEQUENCE_ID_THAT_SHOULD_TRUNCATE_OR_FAIL_SAFELY_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",  // Exact exploit case
        NULL  // Edge case
    };
    
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]) - 1;
    
    for (int i = 0; i < num_payloads; i++) {
        ngx_str_t sequence_id;
        u_char buffer[256];  // Realistic buffer size
        u_char *p = buffer;
        ngx_uint_t timestamp = 1234567890;
        int sequence_id_escape = 0;
        
        // Setup test input
        sequence_id.data = (u_char *)payloads[i];
        sequence_id.len = strlen(payloads[i]);
        
        // Call the actual vulnerable code path
        p = vod_sprintf(p, ID3_TEXT_JSON_SEQUENCE_ID_PREFIX_FORMAT, timestamp);
        if (sequence_id_escape) {
            p = (u_char*)vod_escape_json(p, sequence_id.data, sequence_id.len);
        } else {
            p = vod_copy(p, sequence_id.data, sequence_id.len);
        }
        p = vod_copy(p, ID3_TEXT_JSON_SEQUENCE_ID_SUFFIX, sizeof(ID3_TEXT_JSON_SEQUENCE_ID_SUFFIX));
        
        // Verify we didn't exceed buffer bounds
        size_t bytes_written = p - buffer;
        ck_assert_msg(bytes_written <= sizeof(buffer), 
                     "Buffer overflow detected: wrote %zu bytes into %zu byte buffer with payload '%s'",
                     bytes_written, sizeof(buffer), payloads[i]);
        
        // Verify null termination if applicable
        if (bytes_written < sizeof(buffer)) {
            ck_assert_msg(buffer[bytes_written] == 0 || buffer[bytes_written - 1] == 0,
                         "Buffer not properly terminated with payload '%s'", payloads[i]);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}