/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <hip/hip_runtime.h>
#include <hipblaslt/hipblaslt-ext.hpp>
#include <iostream>

#include "helper.h"

void simpleGemmAlphaVecExt(hipblasLtHandle_t  handle,
                           hipblasOperation_t trans_a,
                           hipblasOperation_t trans_b,
                           int64_t            m,
                           int64_t            n,
                           int64_t            k,
                           int64_t            batch_count,
                           float&             alpha,
                           float&             beta,
                           void*              d_a,
                           void*              d_b,
                           void*              d_c,
                           void*              d_d,
                           void*              d_alphaVec,
                           void*              d_workspace,
                           int64_t            max_workspace_size,
                           hipStream_t        stream);

int main()
{
    /** This is an example using hipblaslt extension API.
     *  This is a NN example with
     *  a = (m, k). lda = m
     *  b = (k, n). ldb = k
     *  c = d = (m, n). ldc = ldd = m
     */
    Runner<hipblasLtHalf, hipblasLtHalf, float, float> runner(
        1024, 512, 1024, 1, 1.f, 1.f, 32 * 1024 * 1024);

    runner.run([&runner] {
        simpleGemmAlphaVecExt(runner.handle,
                              HIPBLAS_OP_N,
                              HIPBLAS_OP_N,
                              runner.m,
                              runner.n,
                              runner.k,
                              runner.batch_count,
                              runner.alpha,
                              runner.beta,
                              runner.d_a,
                              runner.d_b,
                              runner.d_c,
                              runner.d_d,
                              runner.d_alphaVec,
                              runner.d_workspace,
                              runner.max_workspace_size,
                              runner.stream);
    });

    return 0;
}

void simpleGemmAlphaVecExt(hipblasLtHandle_t  handle,
                           hipblasOperation_t trans_a,
                           hipblasOperation_t trans_b,
                           int64_t            m,
                           int64_t            n,
                           int64_t            k,
                           int64_t            batch_count,
                           float&             alpha,
                           float&             beta,
                           void*              d_a,
                           void*              d_b,
                           void*              d_c,
                           void*              d_d,
                           void*              d_alphaVec,
                           void*              d_workspace,
                           int64_t            max_workspace_size,
                           hipStream_t        stream)
{
    hipblaslt_ext::GemmPreference gemmPref;
    gemmPref.setMaxWorkspaceBytes(max_workspace_size);
    hipblaslt_ext::Gemm gemm(handle,
                             trans_a,
                             trans_b,
                             HIPBLASLT_R_16F,
                             HIPBLASLT_R_16F,
                             HIPBLASLT_R_16F,
                             HIPBLASLT_R_16F,
                             HIPBLASLT_COMPUTE_F32);

    hipblaslt_ext::GemmEpilogue
        epilogue; // No action needed, default is HIPBLASLT_EPILOGUE_DEFAULT. (Gemm only)
    hipblaslt_ext::GemmInputs inputs;
    inputs.a             = d_a;
    inputs.b             = d_b;
    inputs.c             = d_c;
    inputs.d             = d_d;
    inputs.alpha         = &alpha;
    inputs.beta          = &beta;
    inputs.scaleAlphaVec = d_alphaVec; // use AlphaVec mode
    gemm.setProblem(m, n, k, batch_count, epilogue, inputs);

    const int                                     request_solutions = 1;
    std::vector<hipblasLtMatmulHeuristicResult_t> heuristicResult;
    CHECK_HIPBLASLT_ERROR(gemm.algoGetHeuristic(request_solutions, gemmPref, heuristicResult));

    if(heuristicResult.empty())
    {
        std::cerr << "No valid solution found!" << std::endl;
        return;
    }

    // In this sample, the workspace is already allocated with max_workspace_size
    // If not, calculate the needed workspace_size and allocate d_workspace here
    // uint64_t workspace_size = 0;
    // for(int i = 0; i < returnedAlgoCount; i++)
    //     workspace_size = max(workspace_size, heuristicResult[i].workspaceSize);
    // CHECK_HIP_ERRORhipMalloc(&d_workspace, workspace_size));
    if(max_workspace_size < (int64_t)heuristicResult[0].workspaceSize)
    {
        std::cerr << "Allocated workspace size is too small!" << std::endl;
        return;
    }

    // Make sure to initialize everytime the algo changes
    CHECK_HIPBLASLT_ERROR(gemm.initialize(heuristicResult[0].algo, d_workspace));
    CHECK_HIPBLASLT_ERROR(gemm.run(stream));
    return;
}