echo "🧹 Clean project 🧹"

rm -rf build
find . -type d -name "mocks" -exec rm -rf {} +