dim objCluster 
Set objCluster = CreateObject("MSCluster.Cluster")

objCluster.Open ""

Dim resource
for each resource in objCluster.Resources
  wscript.echo "Resource: " & resource.Name
  dim res2
  for each res2 in resource.Dependencies
     wscript.echo "- dependency: " & res2.Name
  next
  wscript.echo ""  
next
