project(tf2_gc)

# Standalone TF2 item-schema/inventory parser test tool. Deliberately has no
# dependency on stdafx.h, protobuf, or any csgo_gc/launcher/steamworks target
# so it builds independently and fast, and never touches the CS:GO/CS2 build.
add_executable(tf2_schema_test
    inventory.cpp
    item_schema.cpp
    keyvalue.cpp
    main.cpp)

target_compile_features(tf2_schema_test PRIVATE cxx_std_20)

if (MSVC)
    target_compile_options(tf2_schema_test PRIVATE /W4)
else()
    target_compile_options(tf2_schema_test PRIVATE -Wall -Wextra)
endif()
