# Atmosphère 1.10.1 reserves one framebuffer in package3 for custom splash art.
$KefirizOriginalCore = 'a6d37f1eabdc2964e2dba0984af17ebeea6de1167391118faed205d9d631d39a'
$KefirizMeridianCore = '26f3edc3f9c90ab4165f904002475e71f832688e6d5f7764240ab908c7bc4f22'

function Test-KefirizMeridianCore([string]$BundleRoot) {
    if ((HashOf (Child $BundleRoot 'sd/atmosphere/package3')) -cne $KefirizOriginalCore -or
        (HashOf (Child $BundleRoot 'boot/meridian-package3')) -cne $KefirizMeridianCore) {
        throw 'The stock or Meridian Atmosphere boot image failed verification.'
    }
}

function Copy-KefirizBootFile([string]$source,[string]$destination,[string]$previous,[string]$expected) {
    $folder = [IO.Path]::GetDirectoryName($destination)
    $staged = Child $folder ('meridian-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        CopyChecked $source $staged $expected
        Commit $staged $destination $previous $expected
    } finally { if ([IO.File]::Exists($staged)) { [IO.File]::Delete($staged) } }
}

function Install-KefirizMeridianCore([string]$SdRoot,[string]$Active,[string]$BundleRoot) {
    Test-KefirizMeridianCore $BundleRoot
    $live = Child $SdRoot 'atmosphere/package3'
    $backup = Child $Active 'meridian-boot-core/original-package3'
    $before = HashOf $live
    $saved = HashOf $backup
    if ($saved -and $saved -cne $KefirizOriginalCore) { throw 'Meridian boot backup failed verification.' }
    if ($before -ceq $KefirizMeridianCore) { return }
    if ($before -cne $KefirizOriginalCore) { throw 'This Atmosphere boot image does not match the verified 1.10.1 release.' }
    if (!$saved) { Copy-KefirizBootFile $live $backup '' $KefirizOriginalCore }
    Copy-KefirizBootFile (Child $BundleRoot 'boot/meridian-package3') $live $KefirizOriginalCore $KefirizMeridianCore
    Write-Host 'Meridian startup artwork installed in the Atmosphere display buffer.'
}

function Restore-KefirizMeridianCore([string]$SdRoot,[string]$Active,[string]$BundleRoot) {
    $backup = Child $Active 'meridian-boot-core/original-package3'
    $saved = HashOf $backup
    if (!$saved) { return }
    if ($saved -cne $KefirizOriginalCore) { throw 'Meridian boot backup failed verification.' }
    $live = Child $SdRoot 'atmosphere/package3'
    $current = HashOf $live
    if ($current -ceq $KefirizMeridianCore) {
        Copy-KefirizBootFile $backup $live $KefirizMeridianCore $KefirizOriginalCore
        Write-Host 'Original Atmosphere boot image restored.'
    } elseif ($current -cne $KefirizOriginalCore) {
        Write-Host 'A later boot image is present. The Meridian rollback copy is in the installer backup.'
    }
}
