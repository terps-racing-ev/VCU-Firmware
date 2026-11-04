# Use the caller's GCC, or RubyInstaller's DevKit when Windows PATH lacks GCC.
gcc_available = ENV.fetch('PATH', '').split(File::PATH_SEPARATOR).any? do |dir|
  File.file?(File.join(dir.delete('"'), 'gcc.exe'))
end
if Gem.win_platform? && !gcc_available
  begin
    require 'ruby_installer/runtime'
    RubyInstaller::Runtime.enable_msys_apps(if_no_msys: :raise)
  rescue LoadError, StandardError => error
    abort "Host GCC unavailable: add it to PATH or pass CC=<compiler> (#{error.message})"
  end
end

begin
  exec('gcc', *ARGV)
rescue Errno::ENOENT
  abort 'Host GCC unavailable: add it to PATH or pass CC=<compiler>'
end
