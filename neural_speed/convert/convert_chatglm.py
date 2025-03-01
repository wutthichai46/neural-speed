#  Copyright (c) 2023 Intel Corporation
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
import sys
import struct
import json
import numpy as np
from pathlib import Path
import argparse
from typing import (IO, TYPE_CHECKING, Any, Callable, Dict, Iterable, List, Literal, Optional, Sequence, Tuple, TypeVar,
                    Union)
from transformers import AutoModel, AutoModelForCausalLM, AutoTokenizer, AutoConfig
from sentencepiece import SentencePieceProcessor  # type: ignore
import gguf


# ref: https://github.com/openai/gpt-2/blob/master/src/encoder.py
def bytes_to_unicode():
    """
    Returns list of utf-8 byte and a corresponding list of unicode strings.
    The reversible bpe codes work on unicode strings.
    This means you need a large # of unicode characters in your vocab if you want to avoid UNKs.
    When you're at something like a 10B token dataset you end up needing around 5K for decent coverage.
    This is a significant percentage of your normal, say, 32K bpe vocab.
    To avoid that, we want lookup tables between utf-8 bytes and unicode strings.
    And avoids mapping to whitespace/control characters the bpe code barfs on.
    """
    bs = list(range(ord("!"), ord("~") + 1)) + list(range(ord("¡"), ord("¬") + 1)) + list(range(ord("®"), ord("ÿ") + 1))
    cs = bs[:]
    n = 0
    for b in range(2**8):
        if b not in bs:
            bs.append(b)
            cs.append(2**8 + n)
            n += 1

    cs = [chr(n) for n in cs]

    return dict(zip(bs, cs))


class SentencePieceVocab:
    def __init__(self, fname_tokenizer: Path, fname_added_tokens: Optional[Path]) -> None:
        self.sentencepiece_tokenizer = SentencePieceProcessor(str(fname_tokenizer))
        added_tokens: Dict[str, int]
        if fname_added_tokens is not None:
            added_tokens = json.load(open(fname_added_tokens))
        else:
            added_tokens = {}
        vocab_size: int = self.sentencepiece_tokenizer.vocab_size()
        expected_ids = list(range(vocab_size, vocab_size + len(added_tokens)))
        actual_ids = sorted(added_tokens.values())
        if expected_ids != actual_ids:
            raise Exception(
                f"Expected added token IDs to be sequential and start at {len(added_tokens)}; got {actual_ids}")
        items = sorted(added_tokens.items(), key=lambda text_idx: text_idx[1])
        self.added_tokens_list = [text for (text, idx) in items]
        self.vocab_size_base: int = vocab_size
        self.vocab_size: int = self.vocab_size_base + len(self.added_tokens_list)
        self.fname_tokenizer = fname_tokenizer
        self.fname_added_tokens = fname_added_tokens

    def sentencepiece_tokens(self) -> Iterable[Tuple[bytes, float]]:
        tokenizer = self.sentencepiece_tokenizer
        for i in range(tokenizer.vocab_size()):
            text: bytes
            if tokenizer.is_unknown(i):
                text = " \u2047 ".encode("utf-8")
            elif tokenizer.is_control(i):
                text = b""
            elif tokenizer.is_byte(i):
                piece = tokenizer.id_to_piece(i)
                if len(piece) != 6:
                    raise Exception(f"Invalid token: {piece}")
                byte_value = int(piece[3:-1], 16)
                text = struct.pack("B", byte_value)
            else:
                text = tokenizer.id_to_piece(i).replace("\u2581", " ").encode("utf-8")
            score: float = tokenizer.get_score(i)
            yield text, score

    def added_tokens(self) -> Iterable[Tuple[bytes, float]]:
        for text in self.added_tokens_list:
            score = -1000.0
            yield text.encode("utf-8"), score

    def all_tokens(self) -> Iterable[Tuple[bytes, float]]:
        yield from self.sentencepiece_tokens()
        yield from self.added_tokens()

    def __repr__(self) -> str:
        return f"<SentencePieceVocab with {self.vocab_size_base} base tokens and {len(self.added_tokens_list)}\
                added tokens>"


def load_vocab_for_glm1(path: Path) -> SentencePieceVocab:
    # Be extra-friendly and accept either a file or a directory.  Also, if it's
    # a directory, it might be the model directory, and tokenizer.model might
    # be in the parent of that.
    if path.is_dir():
        path2 = path / "ice_text.model"
        # Use `.parent` instead of /.. to handle the symlink case better.
        path3 = path.parent / "ice_text.model"
        if path2.exists():
            path = path2
        elif path3.exists():
            path = path3
        else:
            raise FileNotFoundError(
                f"Could not find tokenizer.model in {path} or its parent; if it's in another directory, \
                pass the directory as --vocab-dir")
    added_tokens_path = path.parent / "added_tokens.json"
    print(f"Loading vocab file {path}")
    return SentencePieceVocab(path, added_tokens_path if added_tokens_path.exists() else None)


def load_vocab_for_glm2(path: Path) -> SentencePieceVocab:
    # Be extra-friendly and accept either a file or a directory.  Also, if it's
    # a directory, it might be the model directory, and tokenizer.model might
    # be in the parent of that.
    if path.is_dir():
        path2 = path / "tokenizer.model"
        # Use `.parent` instead of /.. to handle the symlink case better.
        path3 = path.parent / "tokenizer.model"
        if path2.exists():
            path = path2
        elif path3.exists():
            path = path3
        else:
            raise FileNotFoundError(
                f"Could not find tokenizer.model in {path} or its parent; if it's in another directory, \
                pass the directory as --vocab-dir")
    added_tokens_path = path.parent / "added_tokens.json"
    print(f"Loading vocab file {path}")
    return SentencePieceVocab(path, added_tokens_path if added_tokens_path.exists() else None)


def chatglm2_convert_gguf(model, tokenizer, dir_model, fname_out, ftype, hparams):
    print("ChatGLM-2.gguf converting: ")
    list_vars = model.state_dict()
    for name in list_vars.keys():
        print(name, list_vars[name].shape, list_vars[name].dtype)

    print(hparams)

    gguf_file = fname_out + '.gguf'
    gguf_writer = gguf.GGUFWriter(gguf_file, "chatglm2")

    arch = "chatglm2."
    gguf_writer.add_uint32('magic', 0x67676d66)
    gguf_writer.add_uint32('version', 1)
    gguf_writer.add_uint32('n_vocab', hparams["padded_vocab_size"])
    gguf_writer.add_embedding_length(hparams["hidden_size"])

    gguf_writer.add_uint32('n_mult', 0)
    gguf_writer.add_head_count(hparams["num_attention_heads"])
    gguf_writer.add_head_count_kv(0)
    gguf_writer.add_block_count(hparams["num_layers"])

    gguf_writer.add_rope_dimension_count(0)
    gguf_writer.add_uint32('ftype', ftype)

    gguf_writer.add_context_length(hparams["seq_length"])

    gguf_writer.add_max_alibi_bias(0)

    gguf_writer.add_uint32('clip_qkv', 0)
    gguf_writer.add_uint32('par_res', 0)

    gguf_writer.add_uint32('word_embed_proj_dim', 0)
    gguf_writer.add_uint32('do_layer_norm_before', 0)

    gguf_writer.add_uint32('multi_query_group_num', hparams["multi_query_group_num"])

    gguf_writer.add_feed_forward_length(hparams["ffn_hidden_size"])

    gguf_writer.add_uint32('inner_hidden_size', 0)

    gguf_writer.add_bos_token_id(tokenizer.bos_token_id if tokenizer.bos_token_id is not None else 0)
    gguf_writer.add_eos_token_id(tokenizer.eos_token_id if tokenizer.eos_token_id is not None else 0)
    gguf_writer.add_pad_token_id(tokenizer.pad_token_id if tokenizer.pad_token_id is not None else 0)
    gguf_writer.add_sep_token_id(tokenizer.sep_token_id if tokenizer.sep_token_id is not None else 0)

    def write_vocab_gguf(dir_model):
        print("gguf: get tokenizer metadata")

        tokens: List[bytes] = []
        scores: List[float] = []
        toktypes: List[int] = []

        if Path(dir_model + "/tokenizer.model").is_file():
            # vocab type sentencepiece
            print("gguf: get sentencepiece tokenizer vocab, scores and token types")

            vocab = load_vocab_for_glm2(Path(dir_model))

            # NOTE: `all_tokens` returns the base vocabulary and added tokens
            for text, score in vocab.all_tokens():
                tokens.append(text)
                scores.append(score)

            if Path(dir_model + "/added_tokens.json").is_file():
                with open(dir_model + "/added_tokens.json", "r", encoding="utf-8") as f:
                    addtokens_json = json.load(f)

                    print("gguf: get added tokens")

                    for key in addtokens_json:
                        tokens.append(key.encode("utf-8"))
                        scores.append(-1000.0)
                        toktypes.append(4)  # user-defined token type

            gguf_writer.add_tokenizer_model("chatglm2")
            gguf_writer.add_token_list(tokens)
            gguf_writer.add_token_scores(scores)

        print("gguf: get special token ids")

        if Path(dir_model + "/tokenizer.json").is_file():
            # Look for special tokens in tokenizer.json if it exists

            with open(dir_model + "/tokenizer.json", "r", encoding="utf-8") as f:
                tokenizer = json.load(f)

            if "added_tokens" in tokenizer and Path(dir_model + "/tokenizer_config.json").is_file():

                with open(dir_model + "/tokenizer_config.json", "r", encoding="utf-8") as f:
                    tokenizer_config = json.load(f)

                if "bos_token" in tokenizer_config and tokenizer_config["bos_token"] != None:
                    for key in tokenizer["added_tokens"]:
                        if key["content"] == tokenizer_config["bos_token"]["content"]:
                            gguf_writer.add_bos_token_id(key["id"])

                if "eos_token" in tokenizer_config and tokenizer_config["eos_token"] != None:
                    for key in tokenizer["added_tokens"]:
                        if key["content"] == tokenizer_config["eos_token"]["content"]:
                            gguf_writer.add_eos_token_id(key["id"])

                if "unk_token" in tokenizer_config and tokenizer_config["unk_token"] != None:
                    for key in tokenizer["added_tokens"]:
                        if key["content"] == tokenizer_config["unk_token"]["content"]:
                            gguf_writer.add_unk_token_id(key["id"])

                if "sep_token" in tokenizer_config and tokenizer_config["sep_token"] != None:
                    for key in tokenizer["added_tokens"]:
                        if key["content"] == tokenizer_config["sep_token"]["content"]:
                            gguf_writer.add_sep_token_id(key["id"])

                if "pad_token" in tokenizer_config and tokenizer_config["pad_token"] != None:
                    for key in tokenizer["added_tokens"]:
                        if key["content"] == tokenizer_config["pad_token"]["content"]:
                            gguf_writer.add_pad_token_id(key["id"])
        else:
            # If no tokenizer.json: Look for special tokens in config.json

            if "bos_token_id" in hparams and hparams["bos_token_id"] != None:
                gguf_writer.add_bos_token_id(hparams["bos_token_id"])

            if "eos_token_id" in hparams and hparams["eos_token_id"] != None:
                gguf_writer.add_eos_token_id(hparams["eos_token_id"])

            if "unk_token_id" in hparams and hparams["unk_token_id"] != None:
                gguf_writer.add_unk_token_id(hparams["unk_token_id"])

            if "sep_token_id" in hparams and hparams["sep_token_id"] != None:
                gguf_writer.add_sep_token_id(hparams["sep_token_id"])

            if "pad_token_id" in hparams and hparams["pad_token_id"] != None:
                gguf_writer.add_pad_token_id(hparams["pad_token_id"])

    write_vocab_gguf(dir_model)

    # tensor info
    print("gguf: get tensor metadata")
    for name in list_vars.keys():
        data = list_vars[name].squeeze().numpy()

        print("Processing variable: " + name + " with shape: ", data.shape)
        if 'inv_freq' in name:
            continue

        n_dims = len(data.shape)

        # ftype == 0 -> float32, ftype == 1 -> float16
        ftype_cur = 0
        if ftype != 0:
            if name[-7:] == ".weight" and n_dims == 2:
                print("  Converting to float16")
                data = data.astype(np.float16)
                ftype_cur = 1
            else:
                print("  Converting to float32")
                data = data.astype(np.float32)
                ftype_cur = 0
        else:
            if data.dtype != np.float32:
                print("  Converting to float32")
                data = data.astype(np.float32)
                ftype_cur = 0

        # print(f"[{i+1:{padi}d}/{len(model)}]
        # Writing tensor {name:38s} | size {size:16} | type {lazy_tensor.data_type.name:4}")

        gguf_writer.add_tensor(name, data)

    print("gguf: write header")
    gguf_writer.write_header_to_file()
    print("gguf: write metadata")
    gguf_writer.write_kv_data_to_file()
    print("gguf: write tensors")
    gguf_writer.write_tensors_to_file()

    gguf_writer.close()

    print("Done. Output file: " + fname_out)
    print("")


def chatglm2_convert(model, tokenizer, dir_model, fname_out, ftype, hparams):
    print("ChatGLM-2 converting: ")
    list_vars = model.state_dict()
    for name in list_vars.keys():
        print(name, list_vars[name].shape, list_vars[name].dtype)

    fout = open(fname_out, "wb")

    print(hparams)

    fout.write(struct.pack("i", 0x67676d66))
    fout.write(struct.pack("i", 1))

    fout.write(struct.pack("i", hparams["padded_vocab_size"]))
    fout.write(struct.pack("i", hparams["hidden_size"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", hparams["num_attention_heads"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", hparams["num_layers"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", ftype))
    fout.write(struct.pack("i", hparams["seq_length"]))
    fout.write(struct.pack("f", 0))
    fout.write(struct.pack("f", 0))
    fout.write(struct.pack("i", 0))

    fout.write(struct.pack("i", 0))  # word_embed_proj_dim (for opt)
    fout.write(struct.pack("i", 0))  # do_layer_norm_before (for opt)

    fout.write(struct.pack("i", hparams["multi_query_group_num"]))
    fout.write(struct.pack("i", hparams["ffn_hidden_size"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("f", hparams.get("layernorm_epsilon", 1e-6)))  # rms norm eps
    fout.write(struct.pack("f", 10000.0))  # freq_base
    fout.write(struct.pack("f", 1.0))  # rope_factor

    fout.write(struct.pack("i", tokenizer.bos_token_id if tokenizer.bos_token_id is not None else 1))
    fout.write(struct.pack("i", tokenizer.eos_token_id if tokenizer.eos_token_id is not None else 2))
    fout.write(struct.pack("i", tokenizer.pad_token_id if tokenizer.pad_token_id is not None else -1))
    fout.write(struct.pack("i", tokenizer.sep_token_id if tokenizer.sep_token_id is not None else -1))

    tokenizer_path = Path(tokenizer.vocab_file).parent
    vocab = load_vocab_for_glm2(Path(tokenizer_path))

    counter = 0
    for text, score in vocab.all_tokens():
        fout.write(struct.pack("i", len(text)))
        fout.write(text)
        fout.write(struct.pack("f", score))
        counter += 1

    while counter < hparams["padded_vocab_size"]:
        fout.write(struct.pack("i", len(text)))
        fout.write(text)
        fout.write(struct.pack("f", 0))
        counter += 1

    for name in list_vars.keys():
        data = list_vars[name].squeeze().numpy()
        print("Processing variable: " + name + " with shape: ", data.shape)
        if 'inv_freq' in name:
            continue

        n_dims = len(data.shape)

        # ftype == 0 -> float32, ftype == 1 -> float16
        ftype_cur = 0
        if ftype != 0:
            if name[-7:] == ".weight" and n_dims == 2:
                print("  Converting to float16")
                data = data.astype(np.float16)
                ftype_cur = 1
            else:
                print("  Converting to float32")
                data = data.astype(np.float32)
                ftype_cur = 0
        else:
            if data.dtype != np.float32:
                print("  Converting to float32")
                data = data.astype(np.float32)
                ftype_cur = 0

        # header
        str = name.encode("utf-8")
        fout.write(struct.pack("iii", n_dims, len(str), ftype_cur))
        for i in range(n_dims):
            fout.write(struct.pack("i", data.shape[n_dims - 1 - i]))
        fout.write(str)

        # data
        data.tofile(fout)

    fout.close()

    print("Done. Output file: " + fname_out)
    print("")


def chatglm1_convert(model, tokenizer, dir_model, fname_out, ftype, hparams):
    print("ChatGLM-1 converting: ")
    list_vars = model.state_dict()
    for name in list_vars.keys():
        print(name, list_vars[name].shape, list_vars[name].dtype)

    fout = open(fname_out, "wb")

    print(hparams)

    fout.write(struct.pack("i", 0x67676d66))
    fout.write(struct.pack("i", 1))

    fout.write(struct.pack("i", hparams["vocab_size"]))
    fout.write(struct.pack("i", hparams["hidden_size"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", hparams["num_attention_heads"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", hparams["num_layers"]))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", ftype))
    fout.write(struct.pack("i", hparams["max_sequence_length"]))
    fout.write(struct.pack("f", 0))
    fout.write(struct.pack("f", 0))
    fout.write(struct.pack("i", 0))

    fout.write(struct.pack("i", 0))  # word_embed_proj_dim (for opt)
    fout.write(struct.pack("i", 0))  # do_layer_norm_before (for opt)

    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", 0))
    fout.write(struct.pack("i", hparams["inner_hidden_size"]))
    fout.write(struct.pack("f", hparams.get("rms_norm_eps", 1e-6)))  # rms norm eps
    fout.write(struct.pack("f", 10000.0))  # freq_base
    fout.write(struct.pack("f", 1.0))  # rope_factor

    fout.write(struct.pack("i", tokenizer.bos_token_id if tokenizer.bos_token_id is not None else -1))
    fout.write(struct.pack("i", tokenizer.eos_token_id if tokenizer.eos_token_id is not None else -1))
    fout.write(struct.pack("i", tokenizer.pad_token_id if tokenizer.pad_token_id is not None else -1))
    fout.write(struct.pack("i", tokenizer.sep_token_id if tokenizer.sep_token_id is not None else -1))

    tokenizer_path = Path(tokenizer.vocab_file).parent
    vocab = load_vocab_for_glm1(Path(tokenizer_path))
    counter = 0
    for text, score in vocab.all_tokens():
        fout.write(struct.pack("i", len(text)))
        fout.write(text)
        fout.write(struct.pack("f", score))
        counter += 1

    while counter < hparams["vocab_size"]:
        fout.write(struct.pack("i", len(text)))
        fout.write(text)
        fout.write(struct.pack("f", 0))
        counter += 1

    for name in list_vars.keys():
        data = list_vars[name].squeeze().numpy()
        print("Processing variable: " + name + " with shape: ", data.shape)
        if 'inv_freq' in name:
            continue

        n_dims = len(data.shape)

        # ftype == 0 -> float32, ftype == 1 -> float16
        ftype_cur = 0
        if ftype != 0:
            if name[-7:] == ".weight" and n_dims == 2:
                print("  Converting to float16")
                data = data.astype(np.float16)
                ftype_cur = 14
            else:
                print("  Converting to float32")
                data = data.astype(np.float32)
                ftype_cur = 0
        else:
            if data.dtype != np.float32:
                print("  Converting to float32")
                data = data.astype(np.float32)
                ftype_cur = 0

        # header
        str = name.encode("utf-8")
        fout.write(struct.pack("iii", n_dims, len(str), ftype_cur))
        for i in range(n_dims):
            fout.write(struct.pack("i", data.shape[n_dims - 1 - i]))
        fout.write(str)

        # data
        data.tofile(fout)

    fout.close()

    print("Done. Output file: " + fname_out)
    print("")


def main(args_in: Optional[List[str]] = None) -> None:
    parser = argparse.ArgumentParser(description="Convert a model to a NE compatible file")
    parser.add_argument("--outtype", choices=["f32", "f16"], help="output format (default: based on input)")
    parser.add_argument("--outfile", type=Path, help="path to write to; default: based on input")
    parser.add_argument("model", type=Path, help="directory containing model file")
    parser.add_argument("--format",
                        type=str,
                        default="NE",
                        choices=["NE", "GGUF"],
                        help="convert to the GGUF or NE format")
    args = parser.parse_args(args_in)

    dir_model = args.model.as_posix()
    fname_out = args.outfile.as_posix()

    # possible data types
    #   ftype == 0 -> float32
    #   ftype == 1 -> float16
    ftype = 0
    if args.outtype == "f16":
        ftype = 1

    config = AutoConfig.from_pretrained(dir_model, trust_remote_code=True)
    tokenizer = AutoTokenizer.from_pretrained(dir_model, trust_remote_code=True)
    model = AutoModel.from_pretrained(dir_model, low_cpu_mem_usage=True, trust_remote_code=True)

    hparams = config.to_dict()

    if hasattr(model.config, "multi_query_attention"):
        if args.format == "GGUF":
            chatglm2_convert_gguf(model, tokenizer, dir_model, fname_out, ftype, hparams)
        else:
            chatglm2_convert(model, tokenizer, dir_model, fname_out, ftype, hparams)
    else:
        chatglm1_convert(model, tokenizer, dir_model, fname_out, ftype, hparams)


if __name__ == '__main__':
    main()
