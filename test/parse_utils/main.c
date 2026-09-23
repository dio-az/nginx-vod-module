#include <ngx_core.h>
#include <inttypes.h>
#include <stdio.h>
#include <vod/parse_utils.h>

volatile ngx_cycle_t* ngx_cycle;
ngx_pool_t* pool;
ngx_log_t ngx_log;

#if (NGX_HAVE_VARIADIC_MACROS)

void
ngx_log_error_core(ngx_uint_t level, ngx_log_t* log, ngx_err_t err, const char* fmt, ...)
#else

void
ngx_log_error_core(ngx_uint_t level, ngx_log_t* log, ngx_err_t err, const char* fmt, va_list args)

#endif
{
}

void*
ngx_array_push(ngx_array_t* a) {
	void *elt, *new_elts;

	if (a->nelts >= a->nalloc) {
		new_elts = realloc(a->elts, a->size * a->nalloc * 2);
		if (new_elts == NULL) {
			return NULL;
		}
		a->elts = new_elts;
		a->nalloc *= 2;
	}

	elt = (u_char*)a->elts + a->size * a->nelts;
	a->nelts++;

	return elt;
}

#define assert(cond)                                                              \
	if (!(cond)) {                                                                \
		printf("Error: assertion failed, file=%s line=%d\n", __FILE__, __LINE__); \
		success = FALSE;                                                          \
	}

bool_t
test_parse_guid_string() {
	bool_t success = TRUE;
	static ngx_str_t tests[] = {
		ngx_string("xx"),
		ngx_string("0000000000000000000000000000000000"),
		ngx_string("0000000000000000000000000000"),
		ngx_null_string,
	};
	u_char guid[16];
	ngx_str_t* cur_test;
	ngx_int_t rc;

	for (cur_test = tests; cur_test->len; cur_test++) {
		rc = parse_utils_parse_guid_string(cur_test, guid);
		if (rc != VOD_BAD_DATA) {
			printf(
				"Error: %s - expected %" PRIdPTR " got %" PRIdPTR "\n",
				cur_test->data,
				(vod_status_t)VOD_BAD_DATA,
				rc
			);
			success = FALSE;
		}
	}

	return success;
}

bool_t
test_parse_fixed_base64_string() {
	bool_t success = TRUE;
	static ngx_str_t tests[] = {
		ngx_string("123"),
		ngx_string("12345==="),
		ngx_string("123456=="),
		ngx_string("2xF1CWaBQs21ihR4NI-AwQ=="),
		ngx_string("2xF1CWaBQs21ihR4NI=AwQ=="),
		ngx_null_string,
	};
	ngx_str_t* cur_test;
	u_char str[16];
	ngx_int_t rc;

	for (cur_test = tests; cur_test->len; cur_test++) {
		rc = parse_utils_parse_fixed_base64_string(cur_test, str, sizeof(str));
		if (rc != VOD_BAD_DATA) {
			printf(
				"Error: %s - expected %" PRIdPTR " got %" PRIdPTR "\n",
				cur_test->data,
				(vod_status_t)VOD_BAD_DATA,
				rc
			);
			success = FALSE;
		}
	}

	return success;
}

bool_t
test_parse_variable_base64_string() {
	bool_t success = TRUE;
	static ngx_str_t tests[] = {
		ngx_string("2xF1CWaBQs21ihR4NI-AwQ=="),
		ngx_null_string,
	};
	ngx_str_t* cur_test;
	ngx_str_t str;
	ngx_int_t rc;

	for (cur_test = tests; cur_test->len; cur_test++) {
		rc = parse_utils_parse_variable_base64_string(pool, cur_test, &str);
		if (rc != VOD_BAD_DATA) {
			printf(
				"Error: %s - expected %" PRIdPTR " got %" PRIdPTR "\n",
				cur_test->data,
				(vod_status_t)VOD_BAD_DATA,
				rc
			);
			success = FALSE;
		}
	}

	return success;
}

int
main() {
	pool = ngx_create_pool(1024 * 1024, &ngx_log);

	if (!test_parse_guid_string()) {
		printf("One or more parse GUID tests failed.\n");
		return 1;
	}

	if (!test_parse_fixed_base64_string()) {
		printf("One or more parse fixed base64 tests failed.\n");
		return 1;
	}

	if (!test_parse_variable_base64_string()) {
		printf("One or more parse variable base64 tests failed.\n");
		return 1;
	}

	printf("All tests passed.\n");
	return 0;
}
