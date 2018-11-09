#ifndef RS_AGGREGATE_H__
#define RS_AGGREGATE_H__
#include <result_processor.h>
#include <query.h>
#include "reducer.h"
#include "expr/expression.h"
#include "aggregate_plan.h"
#include <value.h>

#ifndef GROUPBY_C_
typedef struct Grouper Grouper;
#endif

typedef enum {
  QEXEC_F_IS_EXTENDED = 0x01,  // Contains aggregations or projections
  QEXEC_F_SEND_SCORES = 0x02,
  QEXEC_F_SEND_SORTKEYS = 0x04,
  QEXEC_F_SEND_NOFIELDS = 0x08,  // Don't send the contents of the fields
  QEXEC_F_SEND_PAYLOADS = 0x10,
  QEXEC_F_IS_CURSOR = 0x20,  // Is a cursor-type query
  QEXEC_F_SEND_SCHEMA = 0x40,

  /**
   * If this pointer is heap allocated, in which case the pointer itself is
   * freed during AR_Free()
   */
  QEXEC_F_IS_HEAPALLOC = 0x80,

  /** Don't use concurrent execution */
  QEXEC_F_SAFEMODE = 0x100,

} QEFlags;

typedef enum {
  /* sent at least one reply */
  QEXEC_S_SENTONE = 0x01,

  /* Received EOF from iterator */
  QEXEC_S_ITERDONE = 0x02,

  /* Has an error */
  QEXEC_S_ERROR = 0x04,

  /* Output done */
  QEXEC_S_OUTPOUTDONE = 0x08
} QEStateFlags;

typedef struct {
  AGGPlan ap;

  /* Arguments converted to sds. Received on input */
  sds *args;
  size_t nargs;

  /** Search Query */
  const char *query;
  /** Stopwords used for query. This is refcounted here */
  StopWordList *stopwords;
  /** Fields to be output and otherwise processed */
  FieldList outFields;
  /** Options controlling search behavior */
  RSSearchOptions searchopts;

  /** Parsed query tree */
  QueryAST ast;

  /** Root iterator. This is owned by the request */
  IndexIterator *rootiter;

  RedisSearchCtx *sctx;

  /** Resumable context */
  ConcurrentSearchCtx conc;

  /** Context for iterating over the queries themselves */
  QueryIterator qiter;

  /** Used for identifying unique objects across this request */
  uint32_t serial;
  /** Flags controlling query output */
  uint32_t reqflags;

  /** Flags indicating current execution state */
  uint32_t stateflags;

  /** Query timeout in milliseconds */
  uint32_t tmoMS;
  uint32_t tmoPolicy;

  /** Cursor settings */
  unsigned cursorMaxIdle;
  unsigned cursorChunkSize;

  /** Set if the query has "timed out". Unset during each iteration */
  int pause;
} AggregateRequest;

// Don't enable concurrent mode.
#define AGGREGATE_REQUEST_NO_CONCURRENT 0x01

// Only generate the plan
#define AGGREGATE_REQUEST_NO_PARSE_QUERY 0x02

// Don't attempt to open the spec
#define AGGREGATE_REQUEST_SPECLESS 0x04

typedef struct {
  const char *cursorLookupName;  // Override the index name in the SearchCtx
  int flags;                     // AGGREGATE_REQUEST_XXX
} AggregateRequestSettings;

/**
 * Persist the request. This safely converts a stack allocated request to
 * one allocated on the heap. This assumes that `req` lives on the stack.
 *
 * The current implementation simply does a malloc and memcpy, but this is
 * abstracted in case the request's own members contain references to it.
 */
AggregateRequest *AggregateRequest_Persist(AggregateRequest *req);

/******************************************************************************
 ******************************************************************************
 ** Grouper Functions                                                        **
 ******************************************************************************
 ******************************************************************************/

/**
 * Creates a new grouper object. This is equivalent to a GROUPBY clause.
 * A `Grouper` object contains at the minimum, the keys on which it groups
 * (indicated by the srckeys) and the keys on which it outputs (indicated by
 * dstkeys).
 *
 * The Grouper will create a new group for each unique cartesian of values found
 * in srckeys within each row, and invoke associated Reducers (can be added via
 * @ref Grouper_AddReducer()) within that context.
 *
 * The srckeys and dstkeys parameters are mirror images of one another, but are
 * necessary because a reducer function will convert and reduce one or more
 * source rows into a single destination row. The srckeys are the values to
 * group by within the source rows, and the dstkeys are the values as they are
 * stored within the destination rows. It is assumed that two RLookups are used
 * like so:
 *
 * @code {.c}
 * RLookup lksrc;
 * RLookup lkdst;
 * const char *kname[] = {"foo", "bar", "baz"};
 * RLookupKey *srckeys[3];
 * RLookupKey *dstkeys[3];
 * for (size_t ii = 0; ii < 3; ++ii) {
 *  srckeys[ii] = RLookup_GetKey(&lksrc, kname[ii], RLOOKUP_F_OCREAT);
 *  dstkeys[ii] = RLookup_GetKey(&lkdst, kname[ii], RLOOKUP_F_OCREAT);
 * }
 * @endcode
 *
 * ResultProcessors (and a grouper is a ResultProcessor) before the grouper
 * should write their data using `lksrc` as a reference point.
 */
Grouper *Grouper_New(const RLookupKey **srckeys, const RLookupKey **dstkeys, size_t n);

/**
 * Gets the result processor associated with the grouper. This is used for building
 * the query pipeline
 */
ResultProcessor *Grouper_GetRP(Grouper *gr);

/**
 * Adds a reducer to the grouper. This must be called before any results are
 * processed by the grouper.
 */
void Grouper_AddReducer(Grouper *g, Reducer *r);

// Entry points
void AggregateCommand_ExecAggregate(RedisModuleCtx *ctx, RedisModuleString **argv, int argc,
                                    struct ConcurrentCmdCtx *cmdCtx);
void AggregateCommand_ExecAggregateEx(RedisModuleCtx *ctx, RedisModuleString **argv, int argc,
                                      struct ConcurrentCmdCtx *cmdCtx,
                                      const AggregateRequestSettings *setings);
void AggregateCommand_ExecCursor(RedisModuleCtx *ctx, RedisModuleString **argv, int argc,
                                 struct ConcurrentCmdCtx *);

void AREQ_Execute(AggregateRequest *req, RedisModuleCtx *outctx);
void AREQ_Free(AggregateRequest *req);
#endif