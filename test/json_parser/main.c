#include <ngx_core.h>
#include <inttypes.h>
#include <stdio.h>
#include <vod/json_parser.h>

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
#define assert_string(val, expected) \
	assert((val).len == sizeof(expected) - 1 && memcmp((val).data, expected, sizeof(expected) - 1) == 0)

static vod_json_status_t
test_json_parse(const char* string, vod_json_value_t* result, u_char* error, size_t error_size) {
	size_t size = ngx_strlen(string) + 1;
	u_char* buffer = ngx_palloc(pool, size);
	if (buffer == NULL) {
		return VOD_JSON_ALLOC_FAILED;
	}

	ngx_memcpy(buffer, string, size);
	return vod_json_parse(pool, buffer, result, error, error_size);
}

bool_t
test_valid_jsons() {
	bool_t success = TRUE;
	vod_json_key_value_t* pairs;
	vod_json_array_t* element;
	vod_json_value_t result;
	ngx_int_t rc;
	u_char error[128];

	rc = test_json_parse(" null ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_NULL);

	rc = test_json_parse(" true ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_BOOL && result.v.boolean);

	rc = test_json_parse(" false ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_BOOL && !result.v.boolean);

	rc = test_json_parse(" \"test\" ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_STRING);
	assert_string(result.v.str, "test");

	rc = test_json_parse(" \"fsdaf\\\"fsaf\nfdasf\\fdfas\" ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_STRING);
	assert_string(result.v.str, "fsdaf\\\"fsaf\nfdasf\\fdfas");

	rc = test_json_parse(" [ ] ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_ARRAY);
	assert(result.v.arr.count == 0);

	// rc = vod_json_parse(pool, (u_char*)" [ null , true , false , \"test\" ] ", &result, error, sizeof(error));
	// assert(rc == VOD_JSON_OK);
	// assert(result.type == VOD_JSON_ARRAY);
	// assert(result.v.arr.count == 4);
	// elements = (vod_json_value_t*)result.v.arr.elts;
	// assert(elements[0].type == VOD_JSON_NULL);
	// assert(elements[1].type == VOD_JSON_BOOL && elements[1].v.boolean);
	// assert(elements[2].type == VOD_JSON_BOOL && !elements[2].v.boolean);
	// assert(elements[3].type == VOD_JSON_STRING);
	// assert_string(elements[3].v.str, "test");

	rc = test_json_parse(" [ [ true ] ] ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_ARRAY);
	assert(result.v.arr.type == VOD_JSON_ARRAY);
	assert(result.v.arr.count == 1);
	element = (vod_json_array_t*)result.v.arr.part.first;
	assert(element->type == VOD_JSON_BOOL);
	assert(element->count == 1);
	assert(*(bool_t*)element->part.first == TRUE);

	// rc = vod_json_parse(pool, (u_char*)" [ [ null ] , false ] ", &result, error, sizeof(error));
	// assert(rc == VOD_JSON_OK);
	// assert(result.type == VOD_JSON_ARRAY);
	// assert(result.v.arr.count == 2);
	// elements = (vod_json_value_t*)result.v.arr.elts;
	// assert(elements[1].type == VOD_JSON_BOOL && !elements[1].v.boolean);
	// assert(elements[0].type == VOD_JSON_ARRAY);
	// elements = (vod_json_value_t*)elements[0].v.arr.elts;
	// assert(elements[0].type == VOD_JSON_NULL);

	rc = test_json_parse(" { } ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_OBJECT);
	assert(result.v.obj.nelts == 0);

	rc = test_json_parse(
		" { \"key1\" : null , \"key2\" : true , \"key3\" : false , \"key4\" : \"value\" }",
		&result,
		error,
		sizeof(error)
	);
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_OBJECT);
	assert(result.v.obj.nelts == 4);
	pairs = (vod_json_key_value_t*)result.v.obj.elts;
	assert_string(pairs[0].key, "key1");
	assert_string(pairs[1].key, "key2");
	assert_string(pairs[2].key, "key3");
	assert_string(pairs[3].key, "key4");
	assert(pairs[0].value.type == VOD_JSON_NULL);
	assert(pairs[1].value.type == VOD_JSON_BOOL && pairs[1].value.v.boolean);
	assert(pairs[2].value.type == VOD_JSON_BOOL && !pairs[2].value.v.boolean);
	assert(pairs[3].value.type == VOD_JSON_STRING);
	assert_string(pairs[3].value.v.str, "value");

	rc = test_json_parse(" { \"key\" : { \"subkey\": \"value\" } } ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_OBJECT);
	assert(result.v.obj.nelts == 1);
	pairs = (vod_json_key_value_t*)result.v.obj.elts;
	assert_string(pairs[0].key, "key");
	assert(pairs[0].value.type == VOD_JSON_OBJECT);
	assert(pairs[0].value.v.obj.nelts == 1);
	pairs = (vod_json_key_value_t*)pairs[0].value.v.obj.elts;
	assert_string(pairs[0].key, "subkey");
	assert(pairs[0].value.type == VOD_JSON_STRING);
	assert_string(pairs[0].value.v.str, "value");

	rc = test_json_parse(
		" { \"key1\" : { \"subkey\": \"value\" } , \"key2\" : null } ", &result, error, sizeof(error)
	);
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_OBJECT);
	assert(result.v.obj.nelts == 2);
	pairs = (vod_json_key_value_t*)result.v.obj.elts;
	assert_string(pairs[1].key, "key2");
	assert(pairs[1].value.type == VOD_JSON_NULL);
	assert_string(pairs[0].key, "key1");
	assert(pairs[0].value.type == VOD_JSON_OBJECT);
	assert(pairs[0].value.v.obj.nelts == 1);
	pairs = (vod_json_key_value_t*)pairs[0].value.v.obj.elts;
	assert_string(pairs[0].key, "subkey");
	assert(pairs[0].value.type == VOD_JSON_STRING);
	assert_string(pairs[0].value.v.str, "value");

	rc = test_json_parse(" { \"key\" : [ \"value\" ] } ", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_OBJECT);
	assert(result.v.obj.nelts == 1);
	pairs = (vod_json_key_value_t*)result.v.obj.elts;
	assert_string(pairs[0].key, "key");
	assert(pairs[0].value.type == VOD_JSON_ARRAY);
	assert(pairs[0].value.v.arr.count == 1);
	assert(pairs[0].value.v.arr.type == VOD_JSON_STRING);
	assert_string(*(vod_str_t*)pairs[0].value.v.arr.part.first, "value");

	rc = test_json_parse(" [ { \"key\" : \"value\" } ]", &result, error, sizeof(error));
	assert(rc == VOD_JSON_OK);
	assert(result.type == VOD_JSON_ARRAY);
	assert(result.v.arr.count == 1);
	assert(result.v.arr.type == VOD_JSON_OBJECT);
	pairs = (vod_json_key_value_t*)((vod_json_object_t*)result.v.arr.part.first)->elts;
	assert_string(pairs[0].key, "key");
	assert(pairs[0].value.type == VOD_JSON_STRING);
	assert_string(pairs[0].value.v.str, "value");

	return success;
}

bool_t
test_bad_jsons() {
	bool_t success = TRUE;
	static char* tests[] = {
		"",
		" ",
		"x",
		"tru",
		"\"fasdf",
		"\"fdasf\\",
		"[\"fdaf]",
		"[\"fdafa\"",
		"[\"fdafa\"}",
		"[\"fdafas\",]",
		"{\"fasd:null}",
		"{\"fasd\",null}",
		"{\"fasd\",\"fdasf}",
		"{\"fasd\",\"fdasf\"]",
		"{\"fasd\",\"fdasf\",}",
		"\"fdsafas\"  x",
		NULL,
	};
	vod_json_value_t result;
	char** cur_test;
	ngx_int_t expected_rc = VOD_JSON_BAD_DATA;
	ngx_int_t rc;
	u_char error[128];

	for (cur_test = tests; *cur_test; cur_test++) {
		rc = test_json_parse(*cur_test, &result, error, sizeof(error));
		if (rc != expected_rc) {
			printf("Error: %s - expected %" PRIdPTR " got %" PRIdPTR "\n", *cur_test, expected_rc, rc);
			success = FALSE;
		}
	}

	return success;
}

int
main() {
	pool = ngx_create_pool(1024 * 1024, &ngx_log);

	if (!test_valid_jsons()) {
		printf("One or more valid JSON tests failed.\n");
		return 1;
	}

	if (!test_bad_jsons()) {
		printf("One or more bad JSON tests failed.\n");
		return 1;
	}

	printf("All tests passed.\n");
	return 0;
}
