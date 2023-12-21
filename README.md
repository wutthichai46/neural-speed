# Neural Speed

Neural Speed is designed to provide the efficient inference of large language models (LLMs) on Intel platforms through the state-of-the-art (SOTA) model compression techniques. The work is highly inspired from [llama.cpp](https://github.com/ggerganov/llama.cpp), which organizes almost all the core code (e.g., kernels) in a single big file with a large number of pre-defined macros, thus making it not easy for developers to support a new model. Our Neural Speed has the following features:

- Modular design to support new models
- [Highly optimized low precision kernels](neural_speed/core/README.md)
- Utilize AMX, VNNI, AVX512F and AVX2 instruction set
- Support CPU (x86 platforms only) and Intel GPU (WIP)
- Support 4bits and 8bits quantization

> Neural Speed is under active development so APIs are subject to change.

## Supported Hardware
| Hardware | Optimization |
|-------------|:-------------:|
|Intel Xeon Scalable Processors | ✔ |
|Intel Xeon CPU Max Series | ✔ |
|Intel Core Processors | ✔ |
|Intel Arc GPU Series | WIP |
|Intel Data Center GPU Max Series | WIP |
|Intel Gaudi2 | Not yet |

## Supported Models

Neural Speed supports the following models:
### Text Generation

<table>
<thead>
  <tr>
    <th rowspan="2">Model Name</th>
    <th colspan="2">INT8</th>
    <th colspan="2">INT4</th>
    <th rowspan="2">Transformer Version</th>
  </tr>
  <tr>
    <th>RTN</th>
    <th>GPTQ</th>
    <th>RTN</th>
    <th>GPTQ</th>
  </tr>
</thead>
<tbody>
  <tr>
    <td><a href="https://huggingface.co/meta-llama/Llama-2-7b-chat-hf" target="_blank" rel="noopener noreferrer">LLaMA2-7B</a>,
    <a href="https://huggingface.co/meta-llama/Llama-2-13b-chat-hf" target="_blank" rel="noopener noreferrer">LLaMA2-13B</a>,
    <a href="https://huggingface.co/meta-llama/Llama-2-70b-chat-hf" target="_blank" rel="noopener noreferrer">LLaMA2-70B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/decapoda-research/llama-7b-hf" target="_blank" rel="noopener noreferrer">LLaMA-7B</a>,
    <a href="https://huggingface.co/decapoda-research/llama-13b-hf" target="_blank" rel="noopener noreferrer">LLaMA-13B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/EleutherAI/gpt-j-6b" target="_blank" rel="noopener noreferrer">GPT-J-6B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/EleutherAI/gpt-neox-20b" target="_blank" rel="noopener noreferrer">GPT-NeoX-20B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/databricks/dolly-v2-3b" target="_blank" rel="noopener noreferrer">Dolly-v2-3B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>4.28.1 or newer</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/mosaicml/mpt-7b" target="_blank" rel="noopener noreferrer">MPT-7B</a>,
    <a href="https://huggingface.co/mosaicml/mpt-30b" target="_blank" rel="noopener noreferrer">MPT-30B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/tiiuae/falcon-7b" target="_blank" rel="noopener noreferrer">Falcon-7B</a>,
    <a href="https://huggingface.co/tiiuae/falcon-40b" target="_blank" rel="noopener noreferrer">Falcon-40B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/bigscience/bloomz-7b1" target="_blank" rel="noopener noreferrer">BLOOM-7B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/facebook/opt-125m" target="_blank" rel="noopener noreferrer">OPT-125m</a>,
    <a href="https://huggingface.co/facebook/opt-1.3b" target="_blank" rel="noopener noreferrer">OPT-1.3B</a>,
    <a href="https://huggingface.co/facebook/opt-13b" target="_blank" rel="noopener noreferrer">OPT-13B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/THUDM/chatglm-6b" target="_blank" rel="noopener noreferrer">ChatGLM-6B</a>,
    <a href="https://huggingface.co/THUDM/chatglm2-6b" target="_blank" rel="noopener noreferrer">ChatGLM2-6B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>4.33.1</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/baichuan-inc/Baichuan-13B-Chat" target="_blank" rel="noopener noreferrer">Baichuan-13B-Chat</a>,
    <a href="https://huggingface.co/baichuan-inc/Baichuan2-13B-Chat" target="_blank" rel="noopener noreferrer">Baichuan2-13B-Chat</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>4.33.1</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/mistralai/Mistral-7B-v0.1" target="_blank" rel="noopener noreferrer">Mistral-7B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>4.34.0 or newer</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/Qwen/Qwen-7B-Chat" target="_blank" rel="noopener noreferrer">Qwen-7B</a>,
    <a href="https://huggingface.co/Qwen/Qwen-14B-Chat" target="_blank" rel="noopener noreferrer">Qwen-14B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
</tbody>
</table>

### Code Generation

<table>
<thead>
  <tr>
    <th rowspan="2">Model Name</th>
    <th colspan="2">INT8</th>
    <th colspan="2">INT4</th>
    <th rowspan="2">Transformer Version</th>
  </tr>
  <tr>
    <th>RTN</th>
    <th>GPTQ</th>
    <th>RTN</th>
    <th>GPTQ</th>
  </tr>
</thead>
<tbody>
  <tr>
    <td><a href="https://huggingface.co/codellama/CodeLlama-7b-hf" target="_blank" rel="noopener noreferrer">Code-LLaMA-7B</a>,
    <a href="https://huggingface.co/codellama/CodeLlama-13b-hf" target="_blank" rel="noopener noreferrer">Code-LLaMA-13B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>✅</td>
    <td>Latest</td>
  </tr>
  <tr>
    <td><a href="https://huggingface.co/bigcode/starcoderbase-1b" target="_blank" rel="noopener noreferrer">StarCoder-1B</a>,
    <a href="https://huggingface.co/bigcode/starcoderbase-3b" target="_blank" rel="noopener noreferrer">StarCoder-3B</a>,
    <a href="https://huggingface.co/bigcode/starcoder" target="_blank" rel="noopener noreferrer">StarCoder-15.5B</a></td>
    <td>✅</td>
    <td>✅</td>
    <td> </td>
    <td> </td>
    <td>Latest</td>
  </tr>
</tbody>
</table>


## Install

### Build Python package
```shell
pip install .
```

### Build executable only

```shell
# Linux and WSL
git submodule update --init --recursive
mkdir build
cd build
cmake .. -G Ninja
ninja
```

```powershell
# Windows
# Install VisualStudio 2022 and open 'Developer PowerShell for VS 2022'
mkdir build
cd build
cmake ..
cmake --build . -j --config Release
```

## How to Use
There are two methods for utilizing the Neural Speed:
- [Transformer-based API](#How-to-use-Transformer-based-API)
- [Straightforward Python script](#How-to-use-Python-script)


## How to use: Transformer-based API

> Please refer to [intel extension for transformers](https://github.com/intel/intel-extension-for-transformers) for detailed usage.

### 1. Basic usage of Running LLM with Transformer-based API

You can use Python API to run Hugging Face model simply. Here is the sample code:
```python
from transformers import AutoTokenizer, TextStreamer
from intel_extension_for_transformers.transformers import AutoModelForCausalLM
model_name = "Intel/neural-chat-7b-v1-1"     # Hugging Face model_id or local model
prompt = "Once upon a time, there existed a little girl,"

tokenizer = AutoTokenizer.from_pretrained(model_name, trust_remote_code=True)
inputs = tokenizer(prompt, return_tensors="pt").input_ids
streamer = TextStreamer(tokenizer)

model = AutoModelForCausalLM.from_pretrained(model_name, load_in_4bit=True)
outputs = model.generate(inputs, streamer=streamer, max_new_tokens=300)
```


## How to use: Python script


> warning: **If you want to use ```from_pretrain``` API**: please follow [Transformer-based API](#How-to-use-Transformer-based-API)

### 1. Run LLM with Python Script
You can run LLM with one-click python script including conversion, quantization and inference.
```
python neural_speed/scripts/run.py model-path --weight_dtype int4 -p "She opened the door and see"
```

Argument description of run.py ([supported MatMul combinations](#supported-matrix-multiplication-data-types-combinations)):
| Argument                    | Description                                                                                                   |
| --------------              | ---------------------------------------------------------------------                                         |
| model                       | Directory containing model file or model id: String                                                           |
| --weight_dtype              | Data type of quantized weight: int4/int8/fp8(=fp8_e4m3)/fp8_e5m2/fp4(=fp4e2m1)/nf4 (default int4)                                                       |
| --alg                       | Quantization algorithm: sym/asym (default sym)                                                                |
| --group_size                | Group size: Int, 32/128/-1 (per channel) (default: 32)                                                                                 |
| --scale_dtype               | Data type of scales: fp32/bf16/fp8 (dafault fp32)                                                                 |
| --compute_dtype             | Data type of Gemm computation: int8/bf16/fp16/fp32 (default: fp32)                                                 |
| --use_ggml                  | Enable ggml for quantization and inference                                                                    |
| -p / --prompt               | Prompt to start generation with: String (default: empty)                                                      |
| -n / --n_predict            | Number of tokens to predict: Int (default: -1, -1 = infinity)                                                 |
| -t / --threads              | Number of threads to use during computation: Int (default: 56)                                                |
| -b / --batch_size_truncate  | Batch size for prompt processing: Int (default: 512)                                                          |
| -c / --ctx_size             | Size of the prompt context: Int (default: 512, can not be larger than specific model's context window length) |
| -s / --seed                 | NG seed: Int (default: -1, use random seed for < 0)                                                           |
| --repeat_penalty            | Penalize repeat sequence of tokens: Float (default: 1.1, 1.0 = disabled)                                      |
| --color                     | Colorise output to distinguish prompt and user input from generations                                         |
| --keep                      | Number of tokens to keep from the initial prompt: Int (default: 0, -1 = all)                                  |
| --shift-roped-k             | Use [ring-buffer](./docs/infinite_inference.md#shift-rope-k-and-ring-buffer) and thus do not re-computing after reaching ctx_size (default: False) |


## Advanced Usage
Besides the one-click script, Neural Speed also offers the detailed script: 1) convert and quantize, and 2) inference.

### 1. Convert and Quantize LLM
Neural Speed assumes the compatible model format as [llama.cpp](https://github.com/ggerganov/llama.cpp) and [ggml](https://github.com/ggerganov/ggml). You can also convert the model by following the below steps:

```bash

# convert the model directly use model id in Hugging Face. (recommended)
python neural_speed/scripts/convert.py --outtype f32 --outfile ne-f32.bin EleutherAI/gpt-j-6b

# or you can download fp32 model (e.g., LLAMA2) from Hugging Face at first, then convert the pytorch model to ggml format.
git clone https://huggingface.co/meta-llama/Llama-2-7b-chat-hf
python neural_speed/scripts/convert.py --outtype f32 --outfile ne-f32.bin model_path

# To convert model with PEFT(Parameter-Efficient Fine-Tuning) adapter, you need to merge the PEFT adapter into the model first, use below command to merge the PEFT adapter and save the merged model, afterwards you can use 'neural_speed/scripts/convert.py' just like above mentioned.
python neural_speed/scripts/load_peft_and_merge.py --model_name_or_path meta-llama/Llama-2-7b-hf --peft_name_or_path dfurman/llama-2-7b-instruct-peft --save_path ./Llama-2-7b-hf-instruct-peft

# quantize weights of fp32 ggml bin
# model_name: llama, llama2, mpt, falcon, gptj, starcoder, dolly
# optimized INT4 model with group size 128 (recommended)
python neural_speed/scripts/quantize.py --model_name llama2 --model_file ne-f32.bin --out_file ne-q4_j.bin --weight_dtype int4 --group_size 128 --compute_dtype int8

# Alternativly you could run ggml q4_0 format like following
python neural_speed/scripts/quantize.py --model_name llama2 --model_file ne-f32.bin --out_file ne-q4_0.bin --weight_dtype int4 --use_ggml
# optimized INT4 model with group size 32
python neural_speed/scripts/quantize.py --model_name llama2 --model_file ne-f32.bin --out_file ne-q4_j.bin --weight_dtype int4 --group_size 32 --compute_dtype int8

```
Argument description of quantize.py ([supported MatMul combinations](#supported-matrix-multiplication-data-types-combinations)):
| Argument        | Description                                                  |
| --------------  | -----------------------------------------------------------  |
| --model_file    | Path to the fp32 model: String                               |
| --out_file      | Path to the quantized model: String                          |
| --build_dir     | Path to the build file: String                               |
| --config        | Path to the configuration file: String (default: "")         |
| --nthread       | Number of threads to use: Int (default: 1)                   |
| --weight_dtype  | Data type of quantized weight: int4/int8/fp8(=fp8_e4m3)/fp8_e5m2/fp4(=fp4_e2m1)/nf4 (default: int4)     |
| --alg           | Quantization algorithm to use: sym/asym (default: sym)       |
| --group_size    | Group size: Int 32/128/-1 (per channel) (default: 32)                                |
| --scale_dtype   | Data type of scales: bf16/fp32/fp8 (default: fp32)               |
| --compute_dtype | Data type of Gemm computation: int8/bf16/fp16/fp32 (default: fp32)|
| --use_ggml      | Enable ggml for quantization and inference                   |

#### Supported Matrix Multiplication Data Types Combinations

Our Neural Speed supports  INT4 / INT8 / FP8 (E4M3, E5M2) / FP4 (E2M1) / NF4 weight-only quantization and FP32 / FP16 / BF16 / INT8 computation forward matmul on the Intel platforms. Here are the all supported data types combinations for matmul operations (quantization and forward).
> This table will be updated frequently due to active development

| Weight dtype | Compute dtype (default value if missing or wrong setting) | Scale dtype (default if missing or wrong setting) | algo (default if missing or wrong setting) |
|---|:---:|:---:|:---:|
| FP32 | FP32 | NA | NA |
| INT8 | INT8 / BF16 / FP16 / FP32 (FP32) | BF16 / FP32 (FP32) | sym / asym (sym) |
| INT4 | INT8 / BF16 / FP16 / FP32 (FP32) | BF16 / FP32 (FP32) | sym / asym (sym) |
| FP8 (E4M3, E5M2) | BF16 / FP16 / FP32 (FP32) | FP8 (FP8) | sym (sym) |
| FP4 (E2M1) | BF16 / FP16 / FP32 (FP32) | BF16 / FP32 (FP32) | sym (sym) |
| NF4 | BF16 / FP16 / FP32 (FP32) | BF16 / FP32 (FP32) | sym (sym) |


### 2. Inference LLM

We provide LLM inference script to run the quantized model. Please reach [us](mailto:itrex.maintainers@intel.com) if you want to run using C++ API directly.
```bash
# recommed to use numactl to bind cores in Intel cpus for better performance
# if you use different core numbers, please also  change -t arg value
# please type prompt about codes when run `StarCoder`, for example, -p "def fibonnaci(".

#Linux and WSL
OMP_NUM_THREADS=<physic_cores> numactl -m 0 -C 0-<physic_cores-1> python neural_speed/scripts/inference.py --model_name llama -m ne-q4_j.bin -c 512 -b 1024 -n 256 -t <physic_cores> --color -p "She opened the door and see"

# if you want to generate fixed outputs, please set --seed arg, for example:
OMP_NUM_THREADS=<physic_cores> numactl -m 0 -C 0-<physic_cores-1> python neural_speed/scripts/inference.py --model_name llama -m ne-q4_j.bin -c 512 -b 1024 -n 256 -t <physic_cores> --color -p "She opened the door and see" --seed 12

# if you want to reduce repeated generated texts, please set --repeat_penalty (value > 1.0, default = 1.0), for example:
OMP_NUM_THREADS=<physic_cores> numactl -m 0 -C 0-<physic_cores-1> python neural_speed/scripts/inference.py --model_name llama -m ne-q4_j.bin -c 512 -b 1024 -n 256 -t <physic_cores> --color -p "She opened the door and see" --repeat_penalty 1.2

#Windows
#Recommend to build and run our project in WSL to get a better and stable performance
python neural_speed/scripts/inference.py --model_name llama -m ne-q4_j.bin -c 512 -b 1024 -n 256 -t <physic_cores|P-cores> --color -p "She opened the door and see"
```

Argument description of inference.py:
| Argument                                          | Description                                                                                                                                                                             |
| --------------                                    | -----------------------------------------------------------------------                                                                                                                 |
| --model_name                                      | Model name: String                                                                                                                                                                      |
| -m / --model                                      | Path to the executed model: String                                                                                                                                                      |
| --build_dir                                       | Path to the build file: String                                                                                                                                                          |
| -p / --prompt                                     | Prompt to start generation with: String (default: empty)                                                                                                                                |
| -n / --n_predict                                  | Number of tokens to predict: Int (default: -1, -1 = infinity)                                                                                                                           |
| -t / --threads                                    | Number of threads to use during computation: Int (default: 56)                                                                                                                          |
| -b / --batch_size                                 | Batch size for prompt processing: Int (default: 512)                                                                                                                                    |
| -c / --ctx_size                                   | Size of the prompt context: Int (default: 512, can not be larger than specific model's context window length)                                                                           |
| -s / --seed                                       | NG seed: Int (default: -1, use random seed for < 0)                                                                                                                                     |
| --repeat_penalty                                  | Penalize repeat sequence of tokens: Float (default: 1.1, 1.0 = disabled)                                                                                                                |
| --color                                           | Colorise output to distinguish prompt and user input from generations                                                                                                                   |
| --keep                                            | Number of tokens to keep from the initial prompt: Int (default: 0, -1 = all)                                                                                                            |
| --shift-roped-k                                   | Use [ring-buffer](./docs/infinite_inference.md#shift-rope-k-and-ring-buffer) and thus do not re-computing after reaching ctx_size (default: False)                                      |
| --glm_tokenizer                                   | The path of the chatglm tokenizer: String (default: THUDM/chatglm-6b)                                                                                                                   |
| --memory-f32 <br> --memory-f16 <br> --memory-auto | Data type of kv memory (default to auto);<br>If set to auto, the runtime will try with jblas flash attn managed format (currently requires GCC11+ & AMX) and fall back to fp16 if failed |


### 3. Tensor Parallelism cross nodes/sockets

We support tensor parallelism strategy for distributed inference/training on multi-node and multi-socket. You can refer to [tensor_parallelism.md](./docs/tensor_parallelism.md) to enable this feature.


### 4. Contribution

You can consider adding your own models via [graph developer document](./developer_document.md).

### 5. Custom Stopping Criteria

You can customize the stopping criteria according to your own needs by processing the input_ids to determine if text generation needs to be stopped.
Here is a simple example, which requires a minimum generation length of 80 tokens. Once the `min_length` is met, encountering a terminator `eos_token_id` will end the generation.

```python
import torch
from typing import List
from transformers import StoppingCriteria, StoppingCriteriaList

class StopOnTokens(StoppingCriteria):
    def __init__(self, min_length: int, start_length: int, stop_token_id: List[int]):
        self.min_length = min_length
        self.start_length = start_length
        self.stop_token_id = stop_token_id

    def __call__(
        self, input_ids: torch.LongTensor, scores: torch.FloatTensor, **kwargs
    ) -> bool:
        if input_ids.shape[-1] - self.start_length > self.min_length:
            for stop_id in self.stop_token_id:
                if input_ids[0][input_ids.shape[-1] - 1] == stop_id:
                    return True
        return False

stopping_criteria = StoppingCriteriaList(
    [
        StopOnTokens(
            min_length=80,
            start_length=inputs.shape[1],
            stop_token_id=[tokenizer.eos_token_id],
        )
    ]
)

outputs = model.generate(inputs, streamer=streamer, stopping_criteria=stopping_criteria)
```

### 6. Perplexity (measuring model quality)
You can use the [scripts/perplexity.py](neural_speed/scripts/perplexity.py) script to over a given (subset of) dataset. Run `python neural_speed/scripts/perplexity.py --help` for detailed usage. For more infomation of the perplexity metric, see https://huggingface.co/docs/transformers/perplexity.
