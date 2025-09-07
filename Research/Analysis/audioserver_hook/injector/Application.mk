# Application.mk
# Application configuration for C++ Injector

# Target Android version
APP_PLATFORM := android-24

# Target architectures - including x86_64 for emulators
# APP_ABI := arm64-v8a armeabi-v7a x86_64 x86

# For specific builds:
# APP_ABI := arm64-v8a  # ARM64 only
APP_ABI := x86_64     # Emulator 64-bit only

# C++ configuration
APP_STL := c++_static
APP_CPPFLAGS := -std=c++14

# Build mode
APP_OPTIM := release

# Compiler flags
APP_CFLAGS := -Wall -Wextra -O2

# PIE for executables
APP_PIE := true
