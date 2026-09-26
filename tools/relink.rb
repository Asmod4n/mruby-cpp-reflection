# The linker command of a build that reflects C++. A reflected class may
# name a function that its headers declare and no linked library
# defines: an extern template, a C++23 member a shared library built
# earlier lacks, a marker only ever named in sizeof. The first link
# names what is missing; the second points each at
# mrb_cpp_reflector::reflect_undefined, which raises NotImplementedError
# when Ruby calls it.
require 'open3'
command = ARGV
output, status = Open3.capture2e({ 'LC_ALL' => 'C' }, *command, '-Wl,--no-demangle')
exit 0 if status.success?
missing = output.scan(/undefined reference to [`']([^']+)'/).flatten.uniq
if missing.empty?
  $stderr.print output
  exit status.exitstatus || 1
end
$stderr.puts "relink: #{missing.size} undefined"
exec(*command, *missing.map { |symbol| "-Wl,--defsym=#{symbol}=_ZN17mrb_cpp_reflector17reflect_undefinedEv" })
