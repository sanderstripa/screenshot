param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
foreach($name in 'monitor','sun','moon') {
    $module=Get-Content (Join-Path $PackageDirectory "dist/esm/icons/$name.js") -Raw -Encoding UTF8
    if($module -notmatch 'lucide-vue-next v0\.468\.0'){throw "Incorrect VPN package version for $name"}
    [xml]$svg=Get-Content "assets/vpn-theme-$name.svg" -Raw -Encoding UTF8
    if($svg.DocumentElement.viewBox -ne '0 0 24 24' -or $svg.DocumentElement.'stroke-width' -ne '2'){throw 'Incorrect SVG metrics'}
    $nodes=@($svg.DocumentElement.ChildNodes)
    $definitions=@([regex]::Matches($module,'\["(rect|line|circle|path)",\s*\{([^}]+)\}\]'))
    if($nodes.Count -ne $definitions.Count){throw "Different shape count in $name"}
    for($i=0;$i -lt $nodes.Count;$i++) {
        if($nodes[$i].LocalName -ne $definitions[$i].Groups[1].Value){throw "Different shape type in $name"}
        foreach($attribute in [regex]::Matches($definitions[$i].Groups[2].Value,'(\w+):\s*"([^"]*)"')) {
            $key=$attribute.Groups[1].Value
            if($key -eq 'key'){continue}
            if($nodes[$i].GetAttribute($key) -cne $attribute.Groups[2].Value){throw "SVG $name differs from the VPN package: $key"}
        }
    }
    "PASS $name SVG matches locked VPN package lucide-vue-next 0.468.0"
}
