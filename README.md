# rdma-analyze

本地对照用的 **Rust RDMA / libibverbs FFI** 资料与工作区：上游 crate 以各自仓库克隆在 **`rdma/`** 子目录（见 `.gitignore`），本仓库主要收录 **`rdma/docs/ffi-schemes/`** 下的方案对比与术语说明。

## 文档入口

见 [rdma/docs/ffi-schemes/README.md](rdma/docs/ffi-schemes/README.md)。

## 子目录克隆（本地）

如需与本目录文档对照阅读，请自行克隆例如：

- [jonhoo/rust-ibverbs](https://github.com/jonhoo/rust-ibverbs)
- [datenlord/rdma-sys](https://github.com/datenlord/rdma-sys)、[datenlord/async-rdma](https://github.com/datenlord/async-rdma)
- [RDMA-Rust/rdma-mummy-sys](https://github.com/RDMA-Rust/rdma-mummy-sys)、[RDMA-Rust/sideway](https://github.com/RDMA-Rust/sideway)
- [youyuanwu/rust-rdma-io](https://github.com/youyuanwu/rust-rdma-io)

克隆后置于 **`rdma/`** 下，目录名与上表仓库名对应即可（例如 `rdma/rust-ibverbs`）。
