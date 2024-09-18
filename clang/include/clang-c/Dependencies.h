/*==-- clang-c/Dependencies.h - Dependency Discovery C Interface --*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a dependency discovery interface similar to           *|
|* clang-scan-deps.                                                           *|
|*                                                                            *|
|* An example of its usage is available in c-index-test/core_main.cpp.        *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_DEPENDENCIES_H
#define LLVM_CLANG_C_DEPENDENCIES_H

#include "clang-c/CXDiagnostic.h"
#include "clang-c/CXErrorCode.h"
#include "clang-c/Platform.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup SCAN_DEPS Dependency scanning service.
 * @{
 */

/**
 * Object encapsulating instance of a dependency scanner service.
 *
 * The dependency scanner service owns the global file system cache and other
 * global state that's shared between the dependency scanner workers.
 *
 * The service provides workers a consistent view of file content throughout its
 * lifetime. A client that wants to see changes to file content should create
 * a new service at the time. For example, a build system might use one service
 * for each build.
 */
typedef struct CXOpaqueDependencyScannerService *CXDependencyScannerService;

/**
 * Options used to construct a \c CXDependencyScannerService.
 */
typedef struct CXOpaqueDependencyScannerServiceOptions
    *CXDependencyScannerServiceOptions;

/**
 * Creates a default set of service options.
 * Must be disposed with \c clang_DependencyScannerServiceOptions_dispose().
 */
CINDEX_LINKAGE CXDependencyScannerServiceOptions
clang_DependencyScannerServiceOptions_create(void);

/**
 * Dispose of a \c CXDependencyScannerServiceOptions object.
 */
CINDEX_LINKAGE void clang_DependencyScannerServiceOptions_dispose(
    CXDependencyScannerServiceOptions);

/**
 * Create a \c CXDependencyScannerService object.
 * Must be disposed with \c clang_DependencyScannerService_dispose().
 */
CINDEX_LINKAGE CXDependencyScannerService
clang_DependencyScannerService_create(CXDependencyScannerServiceOptions Opts);

/**
 * Dispose of a \c CXDependencyScannerService object.
 * The service object must be disposed of after the workers are disposed of.
 */
CINDEX_LINKAGE void
    clang_DependencyScannerService_dispose(CXDependencyScannerService);

/**
 * Object encapsulating instance of a dependency scanner worker.
 *
 * The dependency scanner workers are expected to be used in separate worker
 * threads. An individual worker is not thread safe.
 *
 * Operations on a worker are not thread-safe and should only be used from a
 * single thread at a time. They are intended to be used by a single dedicated
 * thread in a thread pool, but they are not inherently pinned to a thread.
 */
typedef struct CXOpaqueDependencyScannerWorker *CXDependencyScannerWorker;

/**
 * Create a \c CXDependencyScannerWorker object.
 * Must be disposed with \c clang_DependencyScannerWorker_dispose().
 */
CINDEX_LINKAGE CXDependencyScannerWorker
    clang_DependencyScannerWorker_create(CXDependencyScannerService);

CINDEX_LINKAGE void
    clang_DependencyScannerWorker_dispose(CXDependencyScannerWorker);

/**
 * An output file kind needed by module dependencies.
 */
typedef enum {
  CXOutputKind_ModuleFile = 0,
  CXOutputKind_Dependencies = 1,
  CXOutputKind_DependenciesTarget = 2,
  CXOutputKind_SerializedDiagnostics = 3,
} CXOutputKind;

/**
 * A callback that is called to determine the paths of output files for each
 * module dependency. The ModuleFile (pcm) path mapping is mandatory.
 *
 * \param Context the MLOContext that was passed to
 *         \c clang_DependencyScannerWorkerScanSettings_create().
 * \param ModuleName the name of the dependent module.
 * \param ContextHash the context hash of the dependent module.
 *                    See \c clang_DependencyGraphModule_getContextHash().
 * \param OutputKind the kind of module output to lookup.
 * \param[out] Output the output path(s) or name, whose total size must be <=
 *                    \p MaxLen. In the case of multiple outputs of the same
 *                    kind, this can be a null-separated list.
 * \param MaxLen the maximum size of Output.
 *
 * \returns the actual length of Output. If the return value is > \p MaxLen,
 *          the callback will be repeated with a larger buffer.
 */
typedef size_t CXModuleLookupOutputCallback(void *Context,
                                            const char *ModuleName,
                                            const char *ContextHash,
                                            CXOutputKind OutputKind,
                                            char *Output, size_t MaxLen);

/**
 * Output of \c clang_DependencyScannerWorker_getDependencyGraph().
 */
typedef struct CXOpaqueDependencyGraph *CXDependencyGraph;

/**
 * An individual module dependency that is part of an overall compilation
 * \c CXDependencyGraph.
 */
typedef struct CXOpaqueDependencyGraphModule *CXDependencyGraphModule;

/**
 * An individual command-line invocation that is part of an overall compilation
 * \c CXDependencyGraph.
 */
typedef struct CXOpaqueDependencyGraphJob *CXDependencyGraphJob;

/**
 * Settings to use for the \c clang_DependencyScannerWorker_getDependencyGraph()
 * action.
 */
typedef struct CXOpaqueDependencyScannerWorkerScanSettings
    *CXDependencyScannerWorkerScanSettings;

/**
 * Creates a set of settings for \c clang_DependencyScannerWorker_getDependencyGraph().
 * Must be disposed with \c clang_DependencyScannerWorkerScanSettings_dispose().
 * Memory for settings is not copied. Any provided pointers must be valid until
 * the call to \c clang_DependencyScannerWorker_getDependencyGraph().
 *
 * \param Argc the number of compiler invocation arguments (including argv[0]).
 * \param Argv the compiler driver invocation arguments (including argv[0]).
 * \param ModuleName If non-null, the dependencies of the named module are
 *                   returned. Otherwise, the dependencies of the whole
 *                   translation unit are returned.
 * \param WorkingDirectory the directory in which the invocation runs.
 * \param MLOContext the context that will be passed to \c MLO each time it is
 *                   called.
 * \param MLO a callback that is called to determine the paths of output files
 *            for each module dependency. This may receive the same module on
 *            different workers. This callback will be called on the same thread
 *            that called \c clang_DependencyScannerWorker_getDependencyGraph().
 */
CINDEX_LINKAGE CXDependencyScannerWorkerScanSettings
clang_DependencyScannerWorkerScanSettings_create(
    int Argc, const char *const *Argv, const char *ModuleName,
    const char *WorkingDirectory, void *MLOContext,
    CXModuleLookupOutputCallback *MLO);

/**
 * Dispose of a \c CXDependencyScannerWorkerScanSettings object.
 */
CINDEX_LINKAGE void clang_DependencyScannerWorkerScanSettings_dispose(
    CXDependencyScannerWorkerScanSettings);

/**
 * Produces the dependency graph for a particular compiler invocation.
 *
 * \param Settings object created via \c
 * clang_DependencyScannerWorkerScanSettings_create().
 * \param [out] Out A non-NULL pointer to store the resulting dependencies. The
 *              output must be freed by calling \c clang_DependencyGraph_dispose().
 *
 * \returns \c CXError_Success on success; otherwise a non-zero \c CXErrorCode
 * indicating the kind of error. When returning \c CXError_Failure there will
 * be a \c CXDependencyGraph object on \p Out that can be used to get diagnostics via
 * \c clang_DependencyGraph_getDiagnostics().
 */
CINDEX_LINKAGE enum CXErrorCode clang_DependencyScannerWorker_getDependencyGraph(
    CXDependencyScannerWorker, CXDependencyScannerWorkerScanSettings Settings,
    CXDependencyGraph *Out);

/**
 * Dispose of a \c CXDependencyGraph object.
 */
CINDEX_LINKAGE void clang_DependencyGraph_dispose(CXDependencyGraph);

/**
 * \returns the number of \c CXDependencyGraphModule objects in the graph.
 */
CINDEX_LINKAGE size_t clang_DependencyGraph_getModuleCount(CXDependencyGraph);

/**
 * \returns the \c CXDependencyGraphModule object at the given \p Index in a
 * topologically sorted list.
 *
 * The \c CXDependencyGraphModule object is only valid to use while \c CXDependencyGraph is
 * valid.
 */
CINDEX_LINKAGE CXDependencyGraphModule clang_DependencyGraph_getModule(CXDependencyGraph,
                                                         size_t Index);

/**
 * \returns the name of the module. This may include `:` for C++20 module
 * partitions, or a header-name for C++20 header units.
 *
 * The string is only valid to use while the \c CXDependencyGraphModule object is
 * valid.
 */
CINDEX_LINKAGE const char *clang_DependencyGraphModule_getName(CXDependencyGraphModule);

/**
 * \returns the context hash of a module represents the set of compiler options
 * that may make one version of a module incompatible from another. This
 * includes things like language mode, predefined macros, header search paths,
 * etc...
 *
 * Modules with the same name but a different \c ContextHash should be treated
 * as separate modules for the purpose of a build.
 *
 * The string is only valid to use while the \c CXDependencyGraphModule object is
 * valid.
 */
CINDEX_LINKAGE const char *
    clang_DependencyGraphModule_getContextHash(CXDependencyGraphModule);

/**
 * \returns the path to the modulemap file which defines this module. If there's
 * no modulemap (e.g. for a C++ module) returns \c NULL.
 *
 * This can be used to explicitly build this module. This file will
 * additionally appear in \c FileDeps as a dependency.
 *
 * The string is only valid to use while the \c CXDependencyGraphModule object is
 * valid.
 */
CINDEX_LINKAGE const char *
    clang_DependencyGraphModule_getModuleMapPath(CXDependencyGraphModule);

CINDEX_LINKAGE size_t clang_DependencyGraphModule_getFileDepCount(CXDependencyGraphModule);
CINDEX_LINKAGE const char *clang_DependencyGraphModule_getFileDep(CXDependencyGraphModule,
                                                           size_t Index);

/**
 * \returns the list of modules which this module direct depends on.
 *
 * This does include the context hash. The format is
 * `<module-name>:<context-hash>`
 *
 * The strings are only valid to use while the \c CXDependencyGraphModule object is
 * valid.
 */
CINDEX_LINKAGE size_t clang_DependencyGraphModule_getModuleDepCount(CXDependencyGraphModule);
CINDEX_LINKAGE size_t clang_DependencyGraphModule_getModuleDep(CXDependencyGraphModule,
                                                        size_t Index);

/**
 * \returns the canonical command line to build this module.
 *
 * The strings are only valid to use while the \c CXDependencyGraphModule object is
 * valid.
 */
CINDEX_LINKAGE
size_t clang_DependencyGraphModule_getBuildArgumentsCount(CXDependencyGraphModule);
CINDEX_LINKAGE const char *
clang_DependencyGraphModule_getBuildArgument(CXDependencyGraphModule, size_t Index);

/**
 * \returns the number \c CXDependencyGraphJob objects in the graph.
 */
CINDEX_LINKAGE size_t clang_DependencyGraph_getJobCount(CXDependencyGraph);

/**
 * \returns the \c CXDependencyGraphJob object at the given \p Index.
 *
 * The \c CXDependencyGraphJob object is only valid to use while \c CXDependencyGraph is
 * valid.
 */
CINDEX_LINKAGE CXDependencyGraphJob clang_DependencyGraph_getJob(CXDependencyGraph,
                                                               size_t Index);

/**
 * \returns the executable name for the command.
 *
 * The string is only valid to use while the \c CXDependencyGraphJob object is
 * valid.
 */
CINDEX_LINKAGE const char *
    clang_DependencyGraphJob_getExecutable(CXDependencyGraphJob);

/**
 * \returns the canonical command line to build this translation unit.
 *
 * The strings are only valid to use while the \c CXDependencyGraphJob object is
 * valid.
 */
CINDEX_LINKAGE
size_t clang_DependencyGraphJob_getBuildArgumentCount(CXDependencyGraphJob);
CINDEX_LINKAGE const char *
clang_DependencyGraphJob_getBuildArgument(CXDependencyGraphJob, size_t Index);

/**
 * \returns the list of files which this translation unit directly depends on.
 *
 * The strings are only valid to use while the \c CXDependencyGraph object is valid.
 */
CINDEX_LINKAGE
size_t clang_DependencyGraph_getTUFileDepCount(CXDependencyGraph);
CINDEX_LINKAGE const char *clang_DependencyGraph_getTUFileDep(CXDependencyGraph,
                                                       size_t Index);

/**
 * \returns the list of modules which this translation unit direct depends on.
 *
 * This does include the context hash. The format is
 * `<module-name>:<context-hash>`
 *
 * The strings are only valid to use while the \c CXDependencyGraph object is valid.
 */
CINDEX_LINKAGE
size_t clang_DependencyGraph_getTUModuleDepsCount(CXDependencyGraph);
CINDEX_LINKAGE size_t clang_DependencyGraph_getTUModuleDep(CXDependencyGraph, size_t Index);

/**
 * \returns the context hash of the C++20 module this translation unit exports.
 *
 * If the translation unit is not a module then this is empty.
 *
 * The string is only valid to use while the \c CXDependencyGraph object is valid.
 */
CINDEX_LINKAGE
const char *clang_DependencyGraph_getTUContextHash(CXDependencyGraph);

/**
 * \returns The diagnostics emitted during scanning. These must be always freed
 * by calling \c clang_disposeDiagnosticSet().
 */
CINDEX_LINKAGE
CXDiagnosticSet clang_DependencyGraph_getDiagnostics(CXDependencyGraph);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // LLVM_CLANG_C_DEPENDENCIES_H
