#ifndef RIN_H
#define RIN_H

#if defined(_WIN32)
#  if defined(RIN_BUILDING_LIBRARY)
#    define RIN_API __declspec(dllexport)
#  else
#    define RIN_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__)
#  define RIN_API __attribute__((visibility("default")))
#else
#  define RIN_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 C strings. Each call is independent and never changes the working directory.
 * A result owns its text until rin_result_free. NULL means allocation failure.
 * A NULL/empty base directory uses the caller's current directory.
 * Status: 0 PASS, 1 FAIL, 2 input/error, 3 UNKNOWN. Check calls only parse/validate.
 */
typedef struct rin_result rin_result;
RIN_API rin_result *rin_run_file(const char *path);
RIN_API rin_result *rin_run_source(const char *source, const char *base_directory);
RIN_API rin_result *rin_check_file(const char *path);
RIN_API rin_result *rin_check_source(const char *source, const char *base_directory);
RIN_API int rin_result_status(const rin_result *result);
RIN_API const char *rin_result_output(const rin_result *result);
RIN_API const char *rin_result_error(const rin_result *result);
RIN_API void rin_result_free(rin_result *result);
RIN_API const char *rin_version(void);

#ifdef __cplusplus
}
#endif
#endif
