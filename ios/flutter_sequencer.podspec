#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_sequencer.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_sequencer'
  s.version          = '0.0.2'
  s.summary          = 'A new flutter plugin project.'
  s.description      = <<-DESC
A new flutter plugin project.
  DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }

  # ✅ Avoid source duplication
  s.source_files = 'Classes/**/*.{h,m,mm,cpp,swift}', 'prepare.sh'

  # ✅ No public headers — avoids Xcode multiple definition error
  s.public_header_files = []

  # ✅ MERGED xcconfig block
  s.xcconfig = {
    'USER_HEADER_SEARCH_PATHS' => '"${PROJECT_DIR}/.."/Classes/CallbackManager/*,"${PROJECT_DIR}/.."/Classes/Scheduler/*,"${PROJECT_DIR}/.."/Classes/AudioUnit/Sfizz/SfizzDSPKernelAdapter.h',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++2a',
    'CLANG_CXX_LIBRARY' => 'libc++'
  }

  s.dependency 'Flutter'
  s.static_framework = true
  s.platform = :ios, '12.0'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64,i386',
    'ENABLE_TESTABILITY' => 'YES',
    'STRIP_STYLE' => 'non-global',
    'HEADER_SEARCH_PATHS' => '$(PODS_TARGET_SRCROOT)/third_party/sfizz/src'
  }

  s.user_target_xcconfig = {
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64'
  }

  s.swift_version = '5.0'
  s.library = 'c++'
  s.prepare_command = './prepare.sh'
  s.vendored_libraries = 'third_party/sfizz/build/libsfizz_fat.a'
end