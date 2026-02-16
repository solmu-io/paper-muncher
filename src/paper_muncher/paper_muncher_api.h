#ifndef PAPER_MUNCHER_API_H
#define PAPER_MUNCHER_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Initialization / shutdown
 * ------------------------------------------------------------------ */

/**
 * Initialize the paper-muncher engine.
 *
 * bundle_dir - path to the CuteKit build directory containing __res__ folders.
 *              If NULL, uses CK_BUILDDIR environment variable.
 *
 * Returns 0 on success, non-zero on error.
 */
int pm_init(const char* bundle_dir);

/**
 * Cleanup global resources.
 * Safe to call multiple times.
 */
void pm_shutdown(void);

/* ------------------------------------------------------------------
 * HTML → PDF (in-memory)
 * ------------------------------------------------------------------ */

/**
 * Convert an HTML string to a PDF buffer.
 *
 * html         - UTF-8 HTML input
 * options_json - optional JSON string for settings (NULL allowed)
 * out_pdf      - pointer that will receive allocated PDF buffer
 * out_size     - size of PDF buffer in bytes
 *
 * Caller MUST free the buffer using pm_free().
 *
 * Returns 0 on success, non-zero on failure.
 */
int pm_html_to_pdf_buffer(
    const char* html,
    const char* options_json,
    unsigned char** out_pdf,
    unsigned long* out_size
);

/* ------------------------------------------------------------------
 * Memory management
 * ------------------------------------------------------------------ */

/**
 * Free memory allocated by paper-muncher.
 */
void pm_free(void* ptr);

/* ------------------------------------------------------------------
 * Error handling
 * ------------------------------------------------------------------ */

/**
 * Get last error message (thread-local).
 * Returned pointer is owned by paper-muncher.
 */
const char* pm_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* PAPER_MUNCHER_API_H */
