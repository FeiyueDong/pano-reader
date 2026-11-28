# 边界与错误

缺失图片：

![不存在的图片](assets/not-found.png)

未闭合公式：

$$
x^2 + y^2

未知命令：

$$
\notacommand{x} + \frac{1}{2}
$$

超长公式：

$$
\sum_{i=1}^{100} \frac{\alpha_i^2 + \beta_i^2 + \gamma_i^2 + \delta_i^2}{\epsilon_i^2 + \zeta_i^2 + \eta_i^2 + \theta_i^2}
$$

外链图片不应加载：

![外链图片](https://example.com/not-allowed.png)

HTML 注入应按纯文本显示：

<script>alert("x")</script>

普通文本仍应可读。
