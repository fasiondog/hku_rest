#!/bin/bash

# 生成自签名 SSL 证书和私钥
# 用于开发和测试环境

set -e

OUTPUT_DIR="${1:-.}"
CERT_NAME="${2:-server}"

echo "=========================================="
echo "生成自签名 SSL 证书"
echo "=========================================="
echo ""

# 创建输出目录
mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

# 生成私钥（RSA 2048 位）
echo "Generating private key..."
openssl genrsa -out "${CERT_NAME}.key" 2048

# 生成证书签名请求（CSR）
echo "Generating Certificate Signing Request (CSR)..."
openssl req -new -key "${CERT_NAME}.key" \
    -out "${CERT_NAME}.csr" \
    -subj "/C=CN/ST=Shanghai/L=Shanghai/O=Hikyuu/OU=Development/CN=localhost"

# 生成自签名证书（有效期 365 天）
echo "Generating self-signed certificate..."
openssl x509 -req -days 365 \
    -in "${CERT_NAME}.csr" \
    -signkey "${CERT_NAME}.key" \
    -out "${CERT_NAME}.crt"

# 合并证书和私钥为 PEM 格式（httpd 使用）
echo "Creating PEM file..."
cat "${CERT_NAME}.crt" "${CERT_NAME}.key" > "${CERT_NAME}.pem"

# 设置权限
chmod 600 "${CERT_NAME}.key"
chmod 600 "${CERT_NAME}.pem"

echo ""
echo "=========================================="
echo "✓ 证书生成成功!"
echo "=========================================="
echo ""
echo "生成的文件:"
echo "  - ${CERT_NAME}.key  : RSA 私钥"
echo "  - ${CERT_NAME}.crt  : 自签名证书"
echo "  - ${CERT_NAME}.csr  : 证书签名请求"
echo "  - ${CERT_NAME}.pem  : 合并的 PEM 文件（供 httpd 使用）"
echo ""
echo "使用方法:"
echo "  https_server.set_tls(\"${CERT_NAME}.pem\");"
echo ""
echo "⚠️  注意："
echo "  - 这些证书仅用于开发和测试"
echo "  - 生产环境请使用受信任的 CA 签发的证书"
echo "  - 私钥文件请妥善保管，不要提交到版本控制"
echo ""

# 清理 CSR（可选）
read -p "是否删除 CSR 文件？(y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f "${CERT_NAME}.csr"
    echo "已删除 CSR 文件"
fi

echo ""
echo "完成!"
