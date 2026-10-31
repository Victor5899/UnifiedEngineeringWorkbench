/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        ink: {
          950: "#090c10",
          900: "#10151c",
          800: "#171e28",
          700: "#222b38",
          600: "#313c4e",
        },
        mist: {
          100: "#e7eef6",
          300: "#b7c3d4",
          500: "#7f8da3",
        },
        accent: {
          DEFAULT: "#3ecfb2",
          dim: "#1c7f72",
        },
        ok: "#3dcc8a",
        warn: "#e2a04a",
        danger: "#e06c75",
      },
    },
  },
  plugins: [],
};
