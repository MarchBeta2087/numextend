## 变更摘要 / Summary

<!-- 一句话说明改了什么、为什么 -->

## 关联 Issue

<!-- 如有：#12 -->

## 定义完成清单（Definition of done）

- [ ] 单元测试：正常路径 + 每个错误码 + 边界值
- [ ] 黄金对拍（driver + Python 精确参考）已注册 CTest
- [ ] 边界/鲁棒测试：tie 构造 / fuzz / OOM 注入（如适用）
- [ ] 规范化不变式断言
- [ ] 文档同步：design.md 修订记录 + 附录 A + README（如涉及）
- [ ] 本地 `ctest` 全绿 + 至少 2 个额外黄金种子
- [ ] GCC `-Wall -Wextra -Wpedantic` 零告警；MSVC 可编译（如涉及）

## 测试结果

```
<粘贴 ctest 输出摘要>
```

## 备注

<!-- 若某项 Definition of done 不适用或未完成，说明原因 -->
