module attributes {torch.debug_module_name = "Llama3_8B_Decode_FC0"} {
  ml_program.global private mutable @global_seed(dense<0> : tensor<i64>) : tensor<i64>

  func.func @forward(%arg0: tensor<1x4096xf32>) -> tensor<1x4096xf32> {
    %cst_weight = arith.constant 1.000000e+00 : f32
    %cst_zero   = arith.constant 0.000000e+00 : f32

    // Weight matrix (4096×4096) – this is what gets placed by pim-opt across many banks
    %weights = tensor.empty() : tensor<4096x4096xf32>
    %filled_weights = linalg.fill ins(%cst_weight : f32)
                                 outs(%weights : tensor<4096x4096xf32>)
                                 -> tensor<4096x4096xf32>

    // Output buffer (1×4096)
    %out_buf = tensor.empty() : tensor<1x4096xf32>
    %zeroed = linalg.fill ins(%cst_zero : f32)
                          outs(%out_buf : tensor<1x4096xf32>)
                          -> tensor<1x4096xf32>

    // The actual GEMM – this is the only compute kernel per token
    %result = linalg.matmul {
                global_layer_idx = 0 : i32,
                layer_group      = 0 : i32,
                num_banks        = 16 : i32,   // keep your original layout info
                type_layer_idx   = 0 : i32
              }
              ins(%arg0, %filled_weights : tensor<1x4096xf32>, tensor<4096x4096xf32>)
              outs(%zeroed : tensor<1x4096xf32>)
              -> tensor<1x4096xf32>

    return %result : tensor<1x4096xf32>
  }
}