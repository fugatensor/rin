#ifndef SUKA_H
#define SUKA_H

#if defined(_WIN32)
#  if defined(SUKA_BUILDING_LIBRARY)
#    define SUKA_API __declspec(dllexport)
#  else
#    define SUKA_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__)
#  define SUKA_API __attribute__((visibility("default")))
#else
#  define SUKA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 C strings. Each call is independent and never changes the working directory.
 * A result owns its text until suka_result_free. NULL means allocation failure.
 * A NULL/empty base directory uses the caller's current directory.
 * Status: 0 PASS, 1 FAIL, 2 input/error, 3 UNKNOWN. Check calls only parse/validate.
 */
typedef struct suka_result suka_result;
SUKA_API suka_result *suka_run_file(const char *path);
SUKA_API suka_result *suka_run_source(const char *source, const char *base_directory);
SUKA_API suka_result *suka_check_file(const char *path);
SUKA_API suka_result *suka_check_source(const char *source, const char *base_directory);
SUKA_API int suka_result_status(const suka_result *result);
SUKA_API const char *suka_result_output(const suka_result *result);
SUKA_API const char *suka_result_error(const suka_result *result);
SUKA_API void suka_result_free(suka_result *result);
SUKA_API const char *suka_version(void);

#ifdef __cplusplus
}
#endif
#endif
